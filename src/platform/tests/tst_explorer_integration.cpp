//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for explorer_integration's Windows implementation (FILE-15): the registration plan applied to, and
//   removed from, a real registry. WINDOWS ONLY -- registered in CTest only there.
//
//   A SCRATCH ROOT, NEVER THE LIVE ONE. Every case works under the key HKEY_CURRENT_USER\Software\VJE-Test\<suite>
//   \Classes, which the implementation accepts in place of Software\Classes; the whole scratch tree is deleted after
//   each case, and nothing here can reach the user's real associations. Nor does anything here tell Explorer that
//   associations changed: only the live root does that.
//
//   WHAT A HEADLESS PLAN TEST CANNOT SAY is whether the calls do what the plan says, and that is this suite's subject:
//   that a registration writes exactly the plan, that a second one changes nothing, that removal leaves the root empty
//   while keeping every key and value VJE did not write, that removing nothing succeeds (the uninstaller's case), and
//   that a registration failing part-way leaves no half of itself behind.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/explorer_integration.hpp>

#include <QMap>
#include <QtTest/QtTest>

#include <windows.h>
#include <sddl.h>

using namespace vje::platform;
using namespace vje::platform::explorer_integration;

namespace
{
	const QString SCRATCH_TREE   = QStringLiteral ( "Software\\VJE-Test\\tst_explorer_integration" );
	const QString SCRATCH_PARENT = QStringLiteral ( "Software\\VJE-Test" );
	const QString SCRATCH_ROOT   = SCRATCH_TREE + QStringLiteral ( "\\Classes" );

	const QString WINDOW_PROGRAM       = QStringLiteral ( "C:/Program Files/VJE/vje.exe" );
	const QString OTHER_WINDOW_PROGRAM = QStringLiteral ( "D:/Portable/VJE/vje.exe" );

	// A key that no longer lets its values be set: read-only for everyone. Its owner -- this process -- keeps the right
	// to change the DACL back, which is how the case restores it before the scratch tree is deleted.

	constexpr auto READ_ONLY_DACL = L"D:P(A;;KR;;;WD)";
	constexpr auto FULL_DACL      = L"D:P(A;;KA;;;WD)";

	std::wstring wide ( const QString& text )
	{
		return text.toStdWString ();
	}

	QString full ( const QString& key )
	{
		return SCRATCH_ROOT + QLatin1Char ( '\\' ) + key;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The registry under a key as "<relative key> | <value name>" -> data, walked recursively. An empty key appears as
	// "<relative key>/" with no data, so a key left behind with nothing in it is still seen.
	//-----------------------------------------------------------------------------------------------------------------

	void walk ( HKEY parent, const QString& relative, QMap<QString, QString>& into )
	{
		DWORD subkeys = 0;
		DWORD values  = 0;

		RegQueryInfoKeyW ( parent, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr );

		if ( ( subkeys == 0 ) && ( values == 0 ) && !relative.isEmpty () )
		{
			into.insert ( relative + QLatin1Char ( '/' ), QString () );
		}

		for ( DWORD i = 0; i < values; ++i )
		{
			wchar_t name [ 256 ];
			DWORD   nameLength = 256;
			wchar_t data [ 1024 ] = L"";
			DWORD   dataBytes  = sizeof ( data );
			DWORD   type       = 0;

			if ( RegEnumValueW ( parent, i, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*> ( data ), &dataBytes ) == ERROR_SUCCESS )
			{
				into.insert ( relative + QStringLiteral ( " | " ) + QString::fromWCharArray ( name, nameLength ),
				              ( type == REG_SZ ) ? QString::fromWCharArray ( data ) : QStringLiteral ( "<type %1>" ).arg ( type ) );
			}
		}

		for ( DWORD i = 0; i < subkeys; ++i )
		{
			wchar_t name [ 256 ];
			DWORD   nameLength = 256;

			if ( RegEnumKeyExW ( parent, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr ) != ERROR_SUCCESS )
			{
				continue;
			}

			const QString child = QString::fromWCharArray ( name, nameLength );

			HKEY handle = nullptr;

			if ( RegOpenKeyExW ( parent, name, 0, KEY_READ, &handle ) == ERROR_SUCCESS )
			{
				walk ( handle, relative.isEmpty () ? child : relative + QLatin1Char ( '\\' ) + child, into );

				RegCloseKey ( handle );
			}
		}
	}

	QMap<QString, QString> contents ()
	{
		QMap<QString, QString> found;

		HKEY root = nullptr;

		if ( RegOpenKeyExW ( HKEY_CURRENT_USER, wide ( SCRATCH_ROOT ).c_str (), 0, KEY_READ, &root ) == ERROR_SUCCESS )
		{
			walk ( root, QString (), found );

			RegCloseKey ( root );
		}

		return found;
	}

	QMap<QString, QString> expected_contents ( const QString& windowProgram )
	{
		QMap<QString, QString> expected;

		for ( const RegistryValue& value : registration_plan ( windowProgram ) )
		{
			expected.insert ( value.key + QStringLiteral ( " | " ) + value.name, value.data );
		}

		return expected;
	}

	void plant ( const QString& key, const QString& name, const QString& data )
	{
		HKEY handle = nullptr;

		QVERIFY ( RegCreateKeyExW ( HKEY_CURRENT_USER, wide ( full ( key ) ).c_str (), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr ) == ERROR_SUCCESS );

		const std::wstring nameText = wide ( name );
		const std::wstring dataText = wide ( data );

		QVERIFY ( RegSetValueExW ( handle, nameText.c_str (), 0, REG_SZ, reinterpret_cast<const BYTE*> ( dataText.c_str () ),
		                           static_cast<DWORD> ( ( dataText.size () + 1 ) * sizeof ( wchar_t ) ) ) == ERROR_SUCCESS );

		RegCloseKey ( handle );
	}

	bool set_dacl ( const QString& key, const wchar_t* sddl )
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;

		if ( !ConvertStringSecurityDescriptorToSecurityDescriptorW ( sddl, SDDL_REVISION_1, &descriptor, nullptr ) )
		{
			return false;
		}

		HKEY handle = nullptr;
		bool applied = false;

		if ( RegOpenKeyExW ( HKEY_CURRENT_USER, wide ( full ( key ) ).c_str (), 0, WRITE_DAC, &handle ) == ERROR_SUCCESS )
		{
			applied = ( RegSetKeySecurity ( handle, DACL_SECURITY_INFORMATION, descriptor ) == ERROR_SUCCESS );

			RegCloseKey ( handle );
		}

		LocalFree ( descriptor );

		return applied;
	}

	void delete_scratch_tree ()
	{
		RegDeleteTreeW ( HKEY_CURRENT_USER, wide ( SCRATCH_TREE ).c_str () );
		RegDeleteKeyW  ( HKEY_CURRENT_USER, wide ( SCRATCH_TREE ).c_str () );

		// The shared parent goes too, once no other suite's scratch tree is in it -- RegDeleteKeyW refuses a key that
		// still has subkeys, which is exactly the condition wanted.

		RegDeleteKeyW ( HKEY_CURRENT_USER, wide ( SCRATCH_PARENT ).c_str () );
	}
}

class TestExplorerIntegration : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void a_registration_writes_exactly_the_plan ();
	void a_second_registration_changes_nothing ();
	void removal_leaves_the_root_empty ();
	void removal_keeps_what_vje_did_not_write ();
	void removing_nothing_succeeds ();
	void the_registration_is_recognised_for_its_own_path_only ();
	void a_registration_failing_part_way_leaves_no_half_behind ();

private:

	QString lockedKey;
};

void TestExplorerIntegration::init ()
{
	delete_scratch_tree ();

	lockedKey.clear ();
}

void TestExplorerIntegration::cleanup ()
{
	if ( !lockedKey.isEmpty () )
	{
		set_dacl ( lockedKey, FULL_DACL );
	}

	delete_scratch_tree ();

	// The live root is never touched: nothing in this suite names it.

	QVERIFY ( SCRATCH_ROOT.compare ( LIVE_CLASSES_ROOT, Qt::CaseInsensitive ) != 0 );
}

void TestExplorerIntegration::a_registration_writes_exactly_the_plan ()
{
	const Outcome registered = register_for ( WINDOW_PROGRAM, SCRATCH_ROOT );

	QVERIFY2 ( registered.succeeded, qPrintable ( registered.problem ) );

	// Exactly: every value the plan names, with its data, and nothing else -- no stray value, and no empty key.

	QCOMPARE ( contents (), expected_contents ( WINDOW_PROGRAM ) );
}

void TestExplorerIntegration::a_second_registration_changes_nothing ()
{
	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	const QMap<QString, QString> first = contents ();

	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	QCOMPARE ( contents (), first );

	// And re-registering from ANOTHER path -- a portable copy moved -- replaces the data rather than adding beside it.

	QVERIFY ( register_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	QCOMPARE ( contents (), expected_contents ( OTHER_WINDOW_PROGRAM ) );
}

void TestExplorerIntegration::removal_leaves_the_root_empty ()
{
	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	const Outcome removed = unregister ( SCRATCH_ROOT );

	QVERIFY2 ( removed.succeeded, qPrintable ( removed.problem ) );

	// Nothing left: no value, and none of the keys the registration created -- SystemFileAssociations and .json
	// included, since in this root VJE created them.

	QCOMPARE ( contents (), ( QMap<QString, QString> () ) );
	QVERIFY ( !is_registered ( SCRATCH_ROOT ) );
}

void TestExplorerIntegration::removal_keeps_what_vje_did_not_write ()
{
	// What a real classes root holds beside VJE's entries (measured on the development machine, 2026-09-26): .json's
	// default value naming the default ProgID, other applications' OpenWithProgids listings, and other extensions under
	// SystemFileAssociations. Plus one key inside VJE's own ProgID that VJE did not write -- removal deletes the values
	// VJE wrote and then only keys left EMPTY, so even that survives.

	plant ( EXTENSION,                                            QString (),                         QStringLiteral ( "json_auto_file" ) );
	plant ( EXTENSION + QStringLiteral ( "\\OpenWithProgids" ),   QStringLiteral ( "VSCode.json" ),   QString () );
	plant ( QStringLiteral ( "SystemFileAssociations\\.json\\shell\\other\\command" ), QString (),    QStringLiteral ( "other.exe \"%1\"" ) );
	plant ( QStringLiteral ( "SystemFileAssociations\\.paint" ),  QString (),                         QStringLiteral ( "paint" ) );
	plant ( PROG_ID + QStringLiteral ( "\\shell\\print\\command" ), QString (),                       QStringLiteral ( "print.exe \"%1\"" ) );

	const QMap<QString, QString> before = contents ();

	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	// The registration sat beside them: it wrote its values into the shared keys and replaced none of theirs.

	QMap<QString, QString> expected = before;

	expected.insert ( expected_contents ( WINDOW_PROGRAM ) );

	QCOMPARE ( contents (), expected );

	QVERIFY ( unregister ( SCRATCH_ROOT ).succeeded );

	QCOMPARE ( contents (), before );
}

void TestExplorerIntegration::removing_nothing_succeeds ()
{
	// The uninstaller's case: VJE was never registered, or already removed. Neither the root nor any key exists.

	const Outcome removed = unregister ( SCRATCH_ROOT );

	QVERIFY2 ( removed.succeeded, qPrintable ( removed.problem ) );
	QCOMPARE ( contents (), ( QMap<QString, QString> () ) );

	// And a second removal after a real one is the same.

	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );
	QVERIFY ( unregister ( SCRATCH_ROOT ).succeeded );
	QVERIFY ( unregister ( SCRATCH_ROOT ).succeeded );
}

void TestExplorerIntegration::the_registration_is_recognised_for_its_own_path_only ()
{
	QVERIFY ( !is_registered ( SCRATCH_ROOT ) );
	QVERIFY ( !is_registered_for ( WINDOW_PROGRAM, SCRATCH_ROOT ) );

	QVERIFY ( register_for ( WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	QVERIFY ( is_registered ( SCRATCH_ROOT ) );
	QVERIFY ( is_registered_for ( WINDOW_PROGRAM, SCRATCH_ROOT ) );

	// A copy elsewhere is NOT registered -- the launch-time check that rewrites the entries for a moved copy.

	QVERIFY ( !is_registered_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ) );

	// One value missing is enough to make the registration incomplete, and enough for is_registered to still see it.

	HKEY handle = nullptr;

	QVERIFY ( RegOpenKeyExW ( HKEY_CURRENT_USER, wide ( full ( PROG_ID + QStringLiteral ( "\\shell\\open" ) ) ).c_str (), 0, KEY_SET_VALUE, &handle ) == ERROR_SUCCESS );
	QVERIFY ( RegDeleteValueW ( handle, L"FriendlyAppName" ) == ERROR_SUCCESS );

	RegCloseKey ( handle );

	QVERIFY ( !is_registered_for ( WINDOW_PROGRAM, SCRATCH_ROOT ) );
	QVERIFY ( is_registered ( SCRATCH_ROOT ) );
}

void TestExplorerIntegration::a_registration_failing_part_way_leaves_no_half_behind ()
{
	// The ProgID's command key exists and refuses a value, so the fourth write of the plan fails after three have
	// succeeded. The registration must report it -- naming the key -- and take the three back out.

	lockedKey = PROG_ID + QStringLiteral ( "\\shell\\open\\command" );

	{
		HKEY handle = nullptr;

		QVERIFY ( RegCreateKeyExW ( HKEY_CURRENT_USER, wide ( full ( lockedKey ) ).c_str (), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &handle, nullptr ) == ERROR_SUCCESS );

		RegCloseKey ( handle );
	}

	QVERIFY ( set_dacl ( lockedKey, READ_ONLY_DACL ) );

	const Outcome registered = register_for ( WINDOW_PROGRAM, SCRATCH_ROOT );

	QVERIFY ( !registered.succeeded );
	QVERIFY2 ( registered.problem.contains ( lockedKey ), qPrintable ( registered.problem ) );

	// No value of the plan is left -- the three that were written are gone. (The locked key itself remains: it was
	// there before, and it cannot be deleted by a process it refuses.)

	QVERIFY ( !is_registered ( SCRATCH_ROOT ) );

	for ( const QString& key : contents ().keys () )
	{
		QVERIFY2 ( key.endsWith ( QLatin1Char ( '/' ) ), qPrintable ( QStringLiteral ( "left behind: " ) + key ) );
	}
}

QTEST_GUILESS_MAIN ( TestExplorerIntegration )

#include "tst_explorer_integration.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for ExplorerIntegrationSync (SET-15, FILE-15): what the running application does to Windows Explorer's
//   menus when the setting changes and at launch. HEADLESS -- the Settings dialog's only part is writing the setting,
//   which a case does directly.
//
//   ON WINDOWS every case works against a scratch registry root under HKEY_CURRENT_USER\Software\VJE-Test, deleted
//   after each case, and a stand-in window program in a temporary directory. ON LINUX the feature does not exist, and
//   the one claim is that the class then does nothing at all.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/ExplorerIntegrationSync.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <platform/explorer_integration.hpp>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

#if defined ( Q_OS_WIN )
#include <windows.h>
#include <sddl.h>
#endif

using namespace vje;

namespace explorer = vje::platform::explorer_integration;

namespace
{
	const QString SCRATCH_TREE   = QStringLiteral ( "Software\\VJE-Test\\tst_explorer_integration_sync" );
	const QString SCRATCH_PARENT = QStringLiteral ( "Software\\VJE-Test" );
	const QString SCRATCH_ROOT   = SCRATCH_TREE + QStringLiteral ( "\\Classes" );

	const QString OTHER_WINDOW_PROGRAM = QStringLiteral ( "D:/Portable/VJE/vje.exe" );

	void delete_scratch_tree ()
	{
#if defined ( Q_OS_WIN )
		RegDeleteTreeW ( HKEY_CURRENT_USER, SCRATCH_TREE.toStdWString ().c_str () );
		RegDeleteKeyW  ( HKEY_CURRENT_USER, SCRATCH_TREE.toStdWString ().c_str () );
		RegDeleteKeyW  ( HKEY_CURRENT_USER, SCRATCH_PARENT.toStdWString ().c_str () );
#endif
	}

#if defined ( Q_OS_WIN )

	// Set a key's DACL from SDDL. Used to make one key refuse writes -- read-only for everyone -- so an attempted
	// rewrite is OBSERVED as a failure rather than inferred. The owner (this process) keeps the right to change the
	// DACL back, which cleanup does before deleting the scratch tree.
	//
	// Not a timestamp: rewriting a value with the data it already holds leaves the key's last-write time unchanged
	// (measured 2026-09-26 -- a neutered "always rewrite" passed a timestamp check), so a timestamp cannot tell a
	// rewrite from none.

	bool set_dacl ( const QString& key, const wchar_t* sddl )
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;

		if ( !ConvertStringSecurityDescriptorToSecurityDescriptorW ( sddl, SDDL_REVISION_1, &descriptor, nullptr ) )
		{
			return false;
		}

		HKEY handle  = nullptr;
		bool applied = false;

		if ( RegOpenKeyExW ( HKEY_CURRENT_USER, key.toStdWString ().c_str (), 0, WRITE_DAC, &handle ) == ERROR_SUCCESS )
		{
			applied = ( RegSetKeySecurity ( handle, DACL_SECURITY_INFORMATION, descriptor ) == ERROR_SUCCESS );

			RegCloseKey ( handle );
		}

		LocalFree ( descriptor );

		return applied;
	}

	constexpr auto READ_ONLY_DACL = L"D:P(A;;KR;;;WD)";
	constexpr auto FULL_DACL      = L"D:P(A;;KA;;;WD)";

#endif
}

class TestExplorerIntegrationSync : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void switching_the_setting_on_registers_this_window_program ();
	void switching_it_off_removes_the_entries ();
	void other_settings_leave_the_registry_alone ();
	void launch_rewrites_entries_that_name_another_copy ();
	void launch_leaves_current_entries_unwritten ();
	void launch_touches_nothing_while_the_setting_is_off ();
	void a_failed_registration_puts_the_setting_back_and_says_why ();
	void nothing_happens_where_the_feature_does_not_exist ();

private:

	std::unique_ptr<QTemporaryDir> directory;
	std::unique_ptr<SettingsStore> settings;
	QString                        windowProgram;
	QString                        lockedKey;                 // Restored to full access by cleanup, when set.
};

void TestExplorerIntegrationSync::init ()
{
	delete_scratch_tree ();

	directory = std::make_unique<QTemporaryDir> ();
	settings  = std::make_unique<SettingsStore> ( directory->filePath ( QStringLiteral ( "settings.json" ) ) );

	// A stand-in for vje.exe: the class checks the program exists before registering it, and nothing launches it.

	windowProgram = directory->filePath ( QStringLiteral ( "vje.exe" ) );

	QFile file ( windowProgram );

	QVERIFY ( file.open ( QIODevice::WriteOnly ) );
}

void TestExplorerIntegrationSync::cleanup ()
{
#if defined ( Q_OS_WIN )
	if ( !lockedKey.isEmpty () )
	{
		set_dacl ( lockedKey, FULL_DACL );

		lockedKey.clear ();
	}
#endif

	settings.reset ();
	directory.reset ();

	delete_scratch_tree ();
}

void TestExplorerIntegrationSync::switching_the_setting_on_registers_this_window_program ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	QSignalSpy failures ( &sync, &ExplorerIntegrationSync::failed );

	// What the Settings dialog's OK does: write the setting. The sync hears it through the store's signal.

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	QVERIFY ( explorer::is_registered_for ( windowProgram, SCRATCH_ROOT ) );
	QCOMPARE ( failures.count (), 0 );
}

void TestExplorerIntegrationSync::switching_it_off_removes_the_entries ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	QVERIFY ( explorer::is_registered ( SCRATCH_ROOT ) );

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, false );

	QVERIFY ( !explorer::is_registered ( SCRATCH_ROOT ) );
}

void TestExplorerIntegrationSync::other_settings_leave_the_registry_alone ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	// Entries for another copy, and the setting on. Any other key changing must not be taken as SET-15 changing.

	QVERIFY ( explorer::register_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	settings->set_bool ( settings_keys::DIAGNOSTIC_LOGGING, true );

	QVERIFY ( explorer::is_registered_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ) );
}

void TestExplorerIntegrationSync::launch_rewrites_entries_that_name_another_copy ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	// A portable copy registered from elsewhere, then moved: the entries name a path that is not this program.

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	QVERIFY ( explorer::register_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	sync.refresh ();

	QVERIFY ( explorer::is_registered_for ( windowProgram, SCRATCH_ROOT ) );
	QVERIFY ( !explorer::is_registered_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ) );
}

void TestExplorerIntegrationSync::launch_leaves_current_entries_unwritten ()
{
#if defined ( Q_OS_WIN )

	// An ordinary launch of the copy that registered: nothing to rewrite, so nothing is written -- which also keeps
	// every start from making Explorer re-read its associations. Observed by making the ProgID's command key refuse
	// writes: a launch that tried to rewrite it would fail, roll back and report, and one that does not try is silent.

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	QVERIFY ( explorer::register_for ( windowProgram, SCRATCH_ROOT ).succeeded );

	lockedKey = SCRATCH_ROOT + QLatin1Char ( '\\' ) + explorer::PROG_ID + QStringLiteral ( "\\shell\\open\\command" );

	QVERIFY ( set_dacl ( lockedKey, READ_ONLY_DACL ) );

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	QSignalSpy failures ( &sync, &ExplorerIntegrationSync::failed );

	sync.refresh ();

	QCOMPARE ( failures.count (), 0 );
	QVERIFY  ( explorer::is_registered_for ( windowProgram, SCRATCH_ROOT ) );
	QCOMPARE ( settings->value_bool ( settings_keys::EXPLORER_INTEGRATION, false ), true );

#else

	QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );

#endif
}

void TestExplorerIntegrationSync::launch_touches_nothing_while_the_setting_is_off ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	// With the setting No, a launch neither registers nor removes: entries some other copy wrote -- whose own setting
	// is its business -- are left exactly as they are.

	QVERIFY ( explorer::register_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	sync.refresh ();

	QVERIFY ( explorer::is_registered_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ) );
	QVERIFY ( !settings->contains ( settings_keys::EXPLORER_INTEGRATION ) );
}

void TestExplorerIntegrationSync::a_failed_registration_puts_the_setting_back_and_says_why ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "Windows Explorer integration exists on Windows only (FILE-15)." );
	}

	// The window program is not there, so registering it would put a menu item in Explorer that does nothing. Another
	// copy's entries are present, and they are what makes the correcting write observable: were it taken for the
	// user's own No, it would remove them.

	QVERIFY ( QFile::remove ( windowProgram ) );
	QVERIFY ( explorer::register_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ).succeeded );

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	QSignalSpy failures ( &sync, &ExplorerIntegrationSync::failed );

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	// Said once, naming the program -- the correcting write back to No is not taken for a second request.

	QCOMPARE ( failures.count (), 1 );
	QVERIFY2 ( failures.front ().front ().toString ().contains ( QStringLiteral ( "vje.exe" ) ), qPrintable ( failures.front ().front ().toString () ) );

	// And the setting tells the truth: this program is not registered, so it reads No -- while the other copy's
	// entries, which nobody asked to remove, are exactly as they were.

	QCOMPARE ( settings->value_bool ( settings_keys::EXPLORER_INTEGRATION, true ), false );
	QVERIFY  ( !explorer::is_registered_for ( windowProgram, SCRATCH_ROOT ) );
	QVERIFY  ( explorer::is_registered_for ( OTHER_WINDOW_PROGRAM, SCRATCH_ROOT ) );
}

void TestExplorerIntegrationSync::nothing_happens_where_the_feature_does_not_exist ()
{
	if ( explorer::is_supported () )
	{
		QSKIP ( "The feature exists on this platform; the cases above cover it." );
	}

	ExplorerIntegrationSync sync ( settings.get (), windowProgram, SCRATCH_ROOT );

	QSignalSpy failures ( &sync, &ExplorerIntegrationSync::failed );

	settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, true );

	sync.refresh ();

	// No failure reported, and the setting untouched: on Linux the dialog never offers the row, so a stored Yes can
	// only have come from a settings file carried over from Windows, and it is left for that platform to act on.

	QCOMPARE ( failures.count (), 0 );
	QCOMPARE ( settings->value_bool ( settings_keys::EXPLORER_INTEGRATION, false ), true );
}

QTEST_GUILESS_MAIN ( TestExplorerIntegrationSync )

#include "tst_explorer_integration_sync.moc"

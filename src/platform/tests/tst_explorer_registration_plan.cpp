//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for explorer_integration's registration plan and removal order (FILE-15) -- HEADLESS, and run on every
//   platform, because the plan is pure: it is the list the Windows implementation applies, and what the entries run,
//   which keys they touch and which they never touch are all properties of the list.
//
//   THE CLAIMS ARE THE REQUIREMENT'S, stated from outside the plan rather than read back from it. Every entry that
//   runs something runs the window program, quoted; VJE writes into a key it does not own only the one value named
//   after its own ProgID; nothing touches .json's default value or the UserChoice keys; and the names do not depend on
//   the path, which is what lets removal work without knowing which copy of VJE registered.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/explorer_integration.hpp>

#include <QSet>
#include <QtTest/QtTest>

using namespace vje::platform;
using namespace vje::platform::explorer_integration;

namespace
{
	// A path with a space in it, as "Program Files" has -- the case that needs the quotes.

	const QString WINDOW_PROGRAM         = QStringLiteral ( "C:/Program Files/VJE/vje.exe" );
	const QString WINDOW_PROGRAM_NATIVE  = QStringLiteral ( "C:\\Program Files\\VJE\\vje.exe" );
	const QString OTHER_WINDOW_PROGRAM   = QStringLiteral ( "D:/Portable/VJE/vje.exe" );

	const QString EDIT_VERB_KEY          = QStringLiteral ( "SystemFileAssociations\\.json\\shell\\VJE.Edit" );
	const QString OPEN_WITH_PROGIDS_KEY  = QStringLiteral ( ".json\\OpenWithProgids" );

	QList<QPair<QString, QString>> names_of ( const std::vector<RegistryValue>& plan )
	{
		QList<QPair<QString, QString>> names;

		for ( const RegistryValue& value : plan )
		{
			names.append ( { value.key, value.name } );
		}

		return names;
	}

	const RegistryValue* find_value ( const std::vector<RegistryValue>& plan, const QString& key, const QString& name )
	{
		for ( const RegistryValue& value : plan )
		{
			if ( ( value.key == key ) && ( value.name == name ) )
			{
				return &value;
			}
		}

		return nullptr;
	}

	// A value's data, or a marker no real value carries -- so an absent value FAILS a comparison rather than crashing
	// the suite, which would take every later case's report down with it.

	QString data_of ( const std::vector<RegistryValue>& plan, const QString& key, const QString& name )
	{
		const RegistryValue* const value = find_value ( plan, key, name );

		return ( value != nullptr ) ? value->data : QStringLiteral ( "<absent: %1 / %2>" ).arg ( key, name );
	}

	bool is_under ( const QString& key, const QString& owner )
	{
		return ( key == owner ) || key.startsWith ( owner + QLatin1Char ( '\\' ) );
	}
}

class TestExplorerRegistrationPlan : public QObject
{
	Q_OBJECT

private slots:

	void every_command_runs_the_window_program_quoted ();
	void every_icon_names_the_window_program ();
	void open_with_reads_vje_and_the_verb_reads_edit_in_vje ();
	void vje_writes_only_under_keys_of_its_own ();
	void nothing_makes_vje_the_default_application ();
	void the_names_do_not_depend_on_the_path ();
	void the_removal_order_is_every_key_on_the_plans_paths_deepest_first ();
	void the_feature_is_offered_on_windows_only ();
};

void TestExplorerRegistrationPlan::every_command_runs_the_window_program_quoted ()
{
	const std::vector<RegistryValue> plan = registration_plan ( WINDOW_PROGRAM );

	// One per menu: Open with's ProgID and the classic menu's verb. Both run the WINDOW program with the file as its one
	// argument -- FILE-01's command-line open -- each quoted, so neither a space in the path nor one in the file name
	// splits an argument.

	const QString expected = QStringLiteral ( "\"%1\" \"%2\"" ).arg ( WINDOW_PROGRAM_NATIVE, QStringLiteral ( "%1" ) );

	int commands = 0;

	for ( const RegistryValue& value : plan )
	{
		if ( value.key.endsWith ( QStringLiteral ( "\\command" ) ) )
		{
			QVERIFY2 ( value.name.isEmpty (), qPrintable ( value.key ) );
			QCOMPARE ( value.data, expected );

			++commands;
		}
	}

	QCOMPARE ( commands, 2 );

	QVERIFY ( find_value ( plan, PROG_ID + QStringLiteral ( "\\shell\\open\\command" ), QString () ) != nullptr );
	QVERIFY ( find_value ( plan, EDIT_VERB_KEY + QStringLiteral ( "\\command" ), QString () ) != nullptr );
}

void TestExplorerRegistrationPlan::every_icon_names_the_window_program ()
{
	const std::vector<RegistryValue> plan = registration_plan ( WINDOW_PROGRAM );

	const QString expected = QStringLiteral ( "\"%1\",0" ).arg ( WINDOW_PROGRAM_NATIVE );

	QCOMPARE ( data_of ( plan, PROG_ID + QStringLiteral ( "\\DefaultIcon" ), QString () ), expected );
	QCOMPARE ( data_of ( plan, EDIT_VERB_KEY, QStringLiteral ( "Icon" ) ), expected );

	// And no value names any OTHER program -- the path given is the only one in the plan.

	for ( const RegistryValue& value : plan )
	{
		if ( value.data.contains ( QStringLiteral ( ".exe" ) ) )
		{
			QVERIFY2 ( value.data.contains ( WINDOW_PROGRAM_NATIVE ), qPrintable ( value.data ) );
		}
	}
}

void TestExplorerRegistrationPlan::open_with_reads_vje_and_the_verb_reads_edit_in_vje ()
{
	const std::vector<RegistryValue> plan = registration_plan ( WINDOW_PROGRAM );

	// MEASURED on Windows 11 (2026-09-26): Open with lists a ProgID's application as "vje.exe" unless the ProgID's
	// shell\open key carries FriendlyAppName, and as "VJE" once it does.

	QCOMPARE ( data_of ( plan, PROG_ID + QStringLiteral ( "\\shell\\open" ), QStringLiteral ( "FriendlyAppName" ) ), QStringLiteral ( "VJE" ) );

	// The ProgID is listed for .json, which is what puts it in Open with at all.

	QVERIFY ( find_value ( plan, OPEN_WITH_PROGIDS_KEY, PROG_ID ) != nullptr );

	// The classic menu shows the verb key's default value.

	QCOMPARE ( data_of ( plan, EDIT_VERB_KEY, QString () ), QStringLiteral ( "Edit in VJE" ) );
}

void TestExplorerRegistrationPlan::vje_writes_only_under_keys_of_its_own ()
{
	// VJE OWNS TWO KEYS -- its ProgID and its verb -- and writes exactly ONE value anywhere else: its ProgID's name in
	// .json's OpenWithProgids, which is a list other applications share. Anything outside that would be VJE editing a
	// registration that is not its own.

	for ( const RegistryValue& value : registration_plan ( WINDOW_PROGRAM ) )
	{
		const bool ownKey          = is_under ( value.key, PROG_ID ) || is_under ( value.key, EDIT_VERB_KEY );
		const bool openWithListing = ( value.key == OPEN_WITH_PROGIDS_KEY ) && ( value.name == PROG_ID );

		QVERIFY2 ( ownKey || openWithListing, qPrintable ( value.key + QStringLiteral ( " / " ) + value.name ) );
	}
}

void TestExplorerRegistrationPlan::nothing_makes_vje_the_default_application ()
{
	// VJE ADDS ITSELF TO WHAT EXPLORER OFFERS AND NEVER CHOOSES FOR THE USER (FILE-15). .json's default value names the
	// default ProgID, and UserChoice -- UserChoiceLatest on current Windows 11 -- under FileExts records the user's own
	// choice behind a hash. The plan writes neither, and touches no FileExts key at all.

	for ( const RegistryValue& value : registration_plan ( WINDOW_PROGRAM ) )
	{
		QVERIFY2 ( !( ( value.key.compare ( EXTENSION, Qt::CaseInsensitive ) == 0 ) && value.name.isEmpty () ), "writes .json's default value" );
		QVERIFY2 ( !value.key.contains ( QStringLiteral ( "UserChoice" ), Qt::CaseInsensitive ), qPrintable ( value.key ) );
		QVERIFY2 ( !value.key.contains ( QStringLiteral ( "FileExts" ), Qt::CaseInsensitive ), qPrintable ( value.key ) );
	}
}

void TestExplorerRegistrationPlan::the_names_do_not_depend_on_the_path ()
{
	// Removal is computed from registration_plan ( QString () ), because an uninstaller -- or a copy of VJE other than
	// the one that registered -- does not know which path was written. That is only right if the keys and names are
	// the same whatever the path; only the data may differ.

	const auto names = names_of ( registration_plan ( WINDOW_PROGRAM ) );

	QCOMPARE ( names_of ( registration_plan ( OTHER_WINDOW_PROGRAM ) ), names );
	QCOMPARE ( names_of ( registration_plan ( QString () ) ), names );

	// And no key or name is written twice, which would make the plan say two things about one value.

	const QSet<QPair<QString, QString>> distinct ( names.begin (), names.end () );

	QCOMPARE ( distinct.size (), names.size () );
}

void TestExplorerRegistrationPlan::the_removal_order_is_every_key_on_the_plans_paths_deepest_first ()
{
	const std::vector<RegistryValue> plan  = registration_plan ( WINDOW_PROGRAM );
	const std::vector<QString>       order = removal_order ( plan );

	// Every key the plan writes a value into, and every ancestor of one below the classes root -- nothing else.

	QSet<QString> expected;

	for ( const RegistryValue& value : plan )
	{
		QString key = value.key;

		while ( !key.isEmpty () )
		{
			expected.insert ( key );

			const qsizetype separator = key.lastIndexOf ( QLatin1Char ( '\\' ) );

			key = ( separator < 0 ) ? QString () : key.left ( separator );
		}
	}

	QCOMPARE ( QSet<QString> ( order.begin (), order.end () ), expected );
	QCOMPARE ( static_cast<qsizetype> ( order.size () ), expected.size () );

	// Deepest first: a parent is examined only after each of its children, so a key emptied by its child's removal
	// is itself removed in the same pass.

	for ( std::size_t i = 0; i < order.size (); ++i )
	{
		for ( std::size_t j = i + 1; j < order.size (); ++j )
		{
			QVERIFY2 ( !order [ j ].startsWith ( order [ i ] + QLatin1Char ( '\\' ) ),
			           qPrintable ( order [ i ] + QStringLiteral ( " precedes its descendant " ) + order [ j ] ) );
		}
	}

	// The classes root itself is never considered: the order is relative to it, so no entry is empty or absolute.

	for ( const QString& key : order )
	{
		QVERIFY ( !key.isEmpty () );
		QVERIFY ( !key.startsWith ( QLatin1Char ( '\\' ) ) );
	}
}

void TestExplorerRegistrationPlan::the_feature_is_offered_on_windows_only ()
{
	// The divergence is stated, not discovered (FILE-15): Windows offers it, and Linux does not for now.

#if defined ( Q_OS_WIN )
	QVERIFY ( is_supported () );
#else
	QVERIFY ( !is_supported () );
	QVERIFY ( !register_for ( WINDOW_PROGRAM ).succeeded );
	QVERIFY ( !is_registered () );
#endif
}

QTEST_GUILESS_MAIN ( TestExplorerRegistrationPlan )

#include "tst_explorer_registration_plan.moc"

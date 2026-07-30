//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for toolbar_catalogue -- the shipped layout, the normalization a stored layout goes through, and the
//   one-time migration off the superseded per-button visibility keys (SET-04).
//
//   THE MIGRATION IS THE CASE THAT MATTERS. It runs exactly once, against a settings file this build will never write
//   again, so there is no way to notice it silently failing except by asserting it here: an existing user's button
//   choices must survive the upgrade, and the old keys must not linger to be read a second time.
//
//   The other sharp edge is that an ABSENT layout and an EMPTY one mean different things -- the shipped default and a
//   deliberately empty toolbar respectively -- which is the whole reason the reader asks contains() rather than testing
//   the list for emptiness.
//
//   Runs in the offscreen GUI harness only because QAction lives in QtGui in Qt 6; nothing here draws anything.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/toolbar_catalogue.hpp"

#include "services/SettingsStore.hpp"

#include <QtTest/QtTest>

#include <QAction>
#include <QSet>
#include <QTemporaryDir>

#include <memory>
#include <vector>

using namespace vje;

class TestToolbarCatalogue : public QObject
{
	Q_OBJECT

private slots:

	void init ();

	void the_default_layout_names_only_real_commands ();
	void the_default_layout_is_a_subset_of_the_catalogue ();
	void normalization_drops_an_entry_naming_no_command ();
	void normalization_keeps_the_first_of_a_repeated_command ();
	void normalization_leaves_every_separator_exactly_where_it_is ();
	void an_absent_key_yields_the_default_layout_and_writes_nothing ();
	void a_stored_empty_layout_is_an_empty_toolbar_not_a_default ();
	void a_stored_layout_is_returned_normalized ();
	void legacy_visibility_keys_migrate_once_and_are_removed ();
	void a_label_loses_its_mnemonic_marker_and_ellipsis ();

private:

	QString settings_path () const;

	// A catalogue naming every command the application offers (32 as of 2026-07-27). Written out here rather than
	// borrowed from MainWindow, which owns the QActions -- so the suite states the expectation independently.

	void build_full_catalogue ();

	QTemporaryDir                         temporaryDirectory;
	std::unique_ptr<SettingsStore>        settings;
	std::vector<ToolbarCommand>           catalogue;
	std::vector<std::unique_ptr<QAction>> actions;
};

QString TestToolbarCatalogue::settings_path () const
{
	return temporaryDirectory.filePath ( QStringLiteral ( "settings.json" ) );
}

void TestToolbarCatalogue::build_full_catalogue ()
{
	namespace names = toolbar_names;

	catalogue.clear ();
	actions.clear ();

	const QStringList everyName =
	{
		names::NEW, names::OPEN, names::CLOSE, names::SAVE, names::SAVE_AS, names::PAGE_SETUP, names::PRINT, names::SETTINGS,
		names::FIND, names::GO_TO, names::UNDO, names::REDO, names::CUT, names::COPY, names::PASTE,
		names::ADD_OBJECT, names::ADD_ARRAY, names::ADD_STRING, names::ADD_NUMBER, names::ADD_BOOLEAN, names::ADD_NULL,
		names::RENAME_KEY, names::DUPLICATE_NODE, names::DELETE_NODE, names::MOVE_UP, names::MOVE_DOWN,
		names::NORMALIZE_ARRAY, names::ARRAY_TO_OBJECTS, names::OBJECTS_TO_ARRAY,
		names::EXPAND_ALL, names::COLLAPSE_ALL,
		names::ABOUT
	};

	for ( const QString& name : everyName )
	{
		actions.push_back ( std::make_unique<QAction> ( name ) );

		catalogue.push_back ( { actions.back ().get (), name } );
	}
}

void TestToolbarCatalogue::init ()
{
	QFile::remove ( settings_path () );

	settings = std::make_unique<SettingsStore> ( settings_path () );

	build_full_catalogue ();
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestToolbarCatalogue::the_default_layout_names_only_real_commands ()
{
	// Section 2.4's four groups: five file commands, the undo pair, the six adds, and the move pair -- separated by
	// three rules. Stated as a shape rather than as the whole list, so re-curating the default does not fail this case
	// while re-STRUCTURING it does.

	const QStringList layout = default_toolbar_layout ();

	QVERIFY ( !layout.isEmpty () );

	QCOMPARE ( layout.count ( toolbar_names::SEPARATOR ), 3 );

	// Neither end carries a rule: the shipped layout has nothing for toolbar_plan to tidy.

	QVERIFY ( layout.first () != toolbar_names::SEPARATOR );
	QVERIFY ( layout.last () != toolbar_names::SEPARATOR );

	// No command appears twice, which normalization would silently correct and so could hide.

	QSet<QString> seen;

	for ( const QString& entry : layout )
	{
		if ( entry == toolbar_names::SEPARATOR )
		{
			continue;
		}

		QVERIFY2 ( !seen.contains ( entry ), qPrintable ( QStringLiteral ( "duplicate default entry: %1" ).arg ( entry ) ) );

		seen.insert ( entry );
	}
}

void TestToolbarCatalogue::the_default_layout_is_a_subset_of_the_catalogue ()
{
	// The catalogue is the eligibility set and the default layout is a curated subset of it -- two separate statements
	// (see the header), which is exactly why they can drift. Normalization against the full catalogue is the identity
	// only if every default entry resolves.

	QCOMPARE ( normalized_toolbar_layout ( default_toolbar_layout (), catalogue ), default_toolbar_layout () );

	// And it IS a proper subset: the catalogue offers commands the bar does not ship with, which is what the Settings
	// dialog's Available list has to show on a first run.

	QVERIFY ( static_cast<int> ( catalogue.size () ) > default_toolbar_layout ().count () );
}

void TestToolbarCatalogue::normalization_drops_an_entry_naming_no_command ()
{
	const QStringList stored = { toolbar_names::NEW, QStringLiteral ( "aCommandFromTheFuture" ), toolbar_names::OPEN };

	QCOMPARE ( normalized_toolbar_layout ( stored, catalogue ),
	           QStringList ( { toolbar_names::NEW, toolbar_names::OPEN } ) );
}

void TestToolbarCatalogue::normalization_keeps_the_first_of_a_repeated_command ()
{
	// A command is on the bar or it is not; it is never on it twice. The FIRST occurrence wins, so the position the
	// user chose first is the one kept.

	const QStringList stored = { toolbar_names::NEW, toolbar_names::OPEN, toolbar_names::NEW };

	QCOMPARE ( normalized_toolbar_layout ( stored, catalogue ),
	           QStringList ( { toolbar_names::NEW, toolbar_names::OPEN } ) );
}

void TestToolbarCatalogue::normalization_leaves_every_separator_exactly_where_it_is ()
{
	// The repeatable entry, and the one thing normalization must NOT tidy: a leading, trailing, or doubled rule is kept
	// verbatim here and collapsed by toolbar_plan at render time, because the layout the dialog shows the user again
	// has to be the one they arranged.

	const QString bar = toolbar_names::SEPARATOR;

	const QStringList stored = { bar, bar, toolbar_names::NEW, bar, bar, toolbar_names::OPEN, bar };

	QCOMPARE ( normalized_toolbar_layout ( stored, catalogue ), stored );
}

void TestToolbarCatalogue::an_absent_key_yields_the_default_layout_and_writes_nothing ()
{
	QVERIFY ( !settings->contains ( settings_keys::TOOLBAR_LAYOUT ) );

	QCOMPARE ( stored_toolbar_layout ( settings.get (), catalogue ), default_toolbar_layout () );

	// A genuine first run writes NOTHING: an untouched installation carries no toolbar key at all, so the shipped
	// layout can be re-curated in a later release and reach users who never opened the Settings dialog.

	QVERIFY ( !settings->contains ( settings_keys::TOOLBAR_LAYOUT ) );
}

void TestToolbarCatalogue::a_stored_empty_layout_is_an_empty_toolbar_not_a_default ()
{
	// The reason the reader asks contains(). An isEmpty() test would restore the default bar on every launch for a user
	// who deliberately emptied it.

	settings->set_string_list ( settings_keys::TOOLBAR_LAYOUT, QStringList () );

	QVERIFY ( stored_toolbar_layout ( settings.get (), catalogue ).isEmpty () );
}

void TestToolbarCatalogue::a_stored_layout_is_returned_normalized ()
{
	const QStringList stored = { toolbar_names::UNDO, QStringLiteral ( "phantom" ), toolbar_names::UNDO, toolbar_names::CUT };

	settings->set_string_list ( settings_keys::TOOLBAR_LAYOUT, stored );

	QCOMPARE ( stored_toolbar_layout ( settings.get (), catalogue ),
	           QStringList ( { toolbar_names::UNDO, toolbar_names::CUT } ) );
}

void TestToolbarCatalogue::legacy_visibility_keys_migrate_once_and_are_removed ()
{
	// A settings file as the superseded implementation left it: one boolean per shipped button, written the first time
	// the user pressed OK in the Settings dialog. Two of them switched off.

	const QString prefix = settings_keys::TOOLBAR_VISIBLE_PREFIX;

	for ( const QString& entry : default_toolbar_layout () )
	{
		if ( entry != toolbar_names::SEPARATOR )
		{
			settings->set_bool ( prefix + entry, true );
		}
	}

	settings->set_bool ( prefix + toolbar_names::CLOSE, false );
	settings->set_bool ( prefix + toolbar_names::ADD_NULL, false );

	QVERIFY ( !settings->contains ( settings_keys::TOOLBAR_LAYOUT ) );

	const QStringList migrated = stored_toolbar_layout ( settings.get (), catalogue );

	// The user's choices survive: the default order, minus exactly the two they switched off.

	QStringList expected = default_toolbar_layout ();

	expected.removeAll ( toolbar_names::CLOSE );
	expected.removeAll ( toolbar_names::ADD_NULL );

	QCOMPARE ( migrated, expected );

	// It is written under the new key...

	QVERIFY ( settings->contains ( settings_keys::TOOLBAR_LAYOUT ) );
	QCOMPARE ( settings->value_string_list ( settings_keys::TOOLBAR_LAYOUT ), expected );

	// ...and the old keys are gone, so nothing reads them a second time and the file carries no trace of the
	// superseded scheme.

	for ( const QString& entry : default_toolbar_layout () )
	{
		if ( entry != toolbar_names::SEPARATOR )
		{
			QVERIFY2 ( !settings->contains ( prefix + entry ), qPrintable ( QStringLiteral ( "legacy key survived: %1" ).arg ( entry ) ) );
		}
	}

	// Running again is a plain read of the new key -- the migration does not repeat and cannot undo an edit made since.

	QCOMPARE ( stored_toolbar_layout ( settings.get (), catalogue ), expected );
}

void TestToolbarCatalogue::a_label_loses_its_mnemonic_marker_and_ellipsis ()
{
	QAction saveAs ( QStringLiteral ( "&Save As..." ) );
	QAction undo ( QStringLiteral ( "&Undo" ) );

	QCOMPARE ( toolbar_command_label ( &saveAs ), QStringLiteral ( "Save As" ) );
	QCOMPARE ( toolbar_command_label ( &undo ), QStringLiteral ( "Undo" ) );
	QCOMPARE ( toolbar_command_label ( nullptr ), QString () );
}

QTEST_MAIN ( TestToolbarCatalogue )

#include "tst_toolbar_catalogue.moc"

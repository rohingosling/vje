//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for settings_schema and SettingsSnapshot -- the Settings dialog's content and its OK / Cancel semantics
//   (SET-01..09), pinned without a dialog. What is asserted:
//
//     - SET-02's group set and ORDER, System last.
//     - Every documented default, and -- the case that matters most -- that those defaults AGREE with the defaults the
//       readers use. The schema states each default a second time (the dialog has to offer one before anything is
//       stored), so the two could drift silently; applying the schema's defaults to an empty store must therefore leave
//       every profile exactly as an empty store already produces it.
//     - The snapshot seeds from the store, holds edits away from it (Cancel is free), and applies them in ONE pass that
//       reports only the keys that actually changed.
//     - SET-09's dependency: the log folder and file name are inert while diagnostic logging is off, and answer to the
//       EDIT state rather than the stored one.
//     - SET-04's Toolbar group: ONE transfer-list field over the window's command catalogue rather than a second list
//       of buttons, and the list-valued setting's one sharp edge -- an empty stored layout is a legal value and must
//       not read as an absent one.
//
//   No dialog is ever opened. It runs in the offscreen GUI harness only because QAction lives in QtGui in Qt 6 and
//   SET-04's group is built from the window's actions; nothing here draws anything.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/settings_schema.hpp"

#include "AppConfig.hpp"
#include "controllers/converters.hpp"
#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"

#include <QtTest/QtTest>

#include <QAction>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

namespace
{
	// The field with this key, from any group. Returns nullptr when the schema does not carry it.

	const SettingsField* find_field ( const std::vector<SettingsGroup>& groups, const QString& key )
	{
		for ( const SettingsGroup& group : groups )
		{
			for ( const SettingsField& field : group.fields )
			{
				if ( field.key == key )
				{
					return &field;
				}
			}
		}

		return nullptr;
	}
}

class TestSettingsSchema : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void the_group_order_is_the_specified_one ();
	void every_group_setting_has_a_field ();
	void the_schema_defaults_match_the_readers_defaults ();
	void a_null_store_reads_one_string_display_for_both_views ();
	void the_snapshot_seeds_from_the_store ();
	void edits_stay_out_of_the_store_until_apply ();
	void apply_reports_only_the_keys_that_changed ();
	void the_system_group_fields_follow_the_logging_toggle ();

	// NFR-06 -- every preference survives a restart.

	void every_schema_setting_survives_a_restart ();
	void the_session_state_keys_survive_a_restart ();
	void the_toolbar_group_is_one_transfer_list_over_the_catalogue ();
	void the_snapshot_tells_an_empty_layout_from_an_absent_one ();
	void restoring_defaults_reseeds_every_field ();

private:

	QString settings_path () const;

	QTemporaryDir                  temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
};

QString TestSettingsSchema::settings_path () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/settings.json" );
}

void TestSettingsSchema::init ()
{
	QVERIFY ( temporaryDirectory.isValid () );

	QFile::remove ( settings_path () );

	settings = std::make_unique<SettingsStore> ( settings_path () );
}

void TestSettingsSchema::cleanup ()
{
	settings.reset ();
}

//---------------------------------------------------------------------------------------------------------------------
// The schema
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsSchema::the_group_order_is_the_specified_one ()
{
	namespace show = config::settings_dialog::show;

	const std::vector<SettingsGroup>& groups = settings_schema ();

	QStringList titles;

	for ( const SettingsGroup& group : groups )
	{
		titles.append ( group.title );
	}

	// SET-02, verbatim -- including System LAST, which is the one position the requirement fixes explicitly. Built from
	// the AppConfig switches so the case states the RULE (this order, minus whatever is switched off) rather than one
	// configuration of it.

	QStringList expected;

	if ( show::GENERAL_GROUP )     { expected.append ( QStringLiteral ( "General" ) ); }
	if ( show::TOOLBAR_GROUP )     { expected.append ( QStringLiteral ( "Toolbar" ) ); }
	if ( show::FORM_VIEW_GROUP )   { expected.append ( QStringLiteral ( "Form View Editor" ) ); }
	if ( show::TEXT_VIEW_GROUP )   { expected.append ( QStringLiteral ( "Text View" ) ); }
	if ( show::CODE_EDITOR_GROUP ) { expected.append ( QStringLiteral ( "Code Editor" ) ); }
	if ( show::PRINTING_GROUP )    { expected.append ( QStringLiteral ( "Printing" ) ); }
	if ( show::SYSTEM_GROUP )      { expected.append ( QStringLiteral ( "System" ) ); }

	QCOMPARE ( titles, expected );

	// The master list never offers an empty page. The Toolbar group is the one exception, and only here: its fields are
	// the window's buttons, filled in by settings_schema_with_toolbar (SET-04).

	for ( const SettingsGroup& group : groups )
	{
		if ( group.title != QStringLiteral ( "Toolbar" ) )
		{
			QVERIFY2 ( !group.fields.empty (), qPrintable ( QStringLiteral ( "empty group: %1" ).arg ( group.title ) ) );
		}
		else
		{
			QVERIFY ( group.fields.empty () );
		}
	}
}

void TestSettingsSchema::every_group_setting_has_a_field ()
{
	namespace show = config::settings_dialog::show;

	const std::vector<SettingsGroup>& groups = settings_schema ();

	// Every key the requirements name as a Settings-dialog value, paired with the AppConfig switches that decide whether
	// the dialog offers it. A key that should be reachable and is not is a setting the user cannot change; a key that is
	// switched off and appears anyway is a switch that does not work. Both are failures, so the assertion is an
	// equivalence rather than a presence check.
	//
	// The pairing is restated here deliberately: a test that read the same table it is checking would agree with the
	// code by construction.

	const QList<QPair<QString, bool>> expectedFields =
	{
		{ settings_keys::THEME,                   show::GENERAL_GROUP     && show::THEME },
		{ settings_keys::CHECK_UPDATES,           show::GENERAL_GROUP     && show::CHECK_UPDATES },
		{ settings_keys::ON_DUPLICATE_KEYS,       show::GENERAL_GROUP     && show::ON_DUPLICATE_KEYS },
		{ settings_keys::ROUNDED_PANE_CORNERS,    show::GENERAL_GROUP     && show::ROUNDED_PANE_CORNERS },
		{ settings_keys::FORM_EDIT_ON,            show::FORM_VIEW_GROUP   && show::FORM_EDIT_ON },
		{ settings_keys::FORM_ALLOW_JAGGED_PASTE, show::FORM_VIEW_GROUP   && show::FORM_ALLOW_JAGGED_PASTE },
		{ settings_keys::FORM_ALLOW_KEY_EDITING,  show::FORM_VIEW_GROUP   && show::FORM_ALLOW_KEY_EDITING },
		{ settings_keys::TEXT_ALIGN_SEPARATORS,   show::TEXT_VIEW_GROUP   && show::TEXT_ALIGN_SEPARATORS },
		{ settings_keys::TEXT_NAME_SEPARATOR,     show::TEXT_VIEW_GROUP   && show::TEXT_NAME_SEPARATOR },
		{ settings_keys::TEXT_INCLUDE_OBJECTS,    show::TEXT_VIEW_GROUP   && show::TEXT_INCLUDE_OBJECTS },
		{ settings_keys::TEXT_INCLUDE_ARRAYS,     show::TEXT_VIEW_GROUP   && show::TEXT_INCLUDE_ARRAYS },
		{ settings_keys::TEXT_MARKDOWN_STYLE,     show::TEXT_VIEW_GROUP   && show::TEXT_MARKDOWN_STYLE },
		{ settings_keys::TEXT_TABLE_STYLE,        show::TEXT_VIEW_GROUP   && show::TEXT_TABLE_STYLE },
		{ settings_keys::CODE_INDENT_KIND,        show::CODE_EDITOR_GROUP && show::CODE_INDENT_KIND },
		{ settings_keys::CODE_INDENT_SIZE,        show::CODE_EDITOR_GROUP && show::CODE_INDENT_SIZE },
		{ settings_keys::CODE_SYNTAX_HIGHLIGHTING, show::CODE_EDITOR_GROUP && show::CODE_SYNTAX_HIGHLIGHTING },
		{ settings_keys::CODE_BRACE_STYLE,        show::CODE_EDITOR_GROUP && show::CODE_BRACE_STYLE },
		{ settings_keys::CODE_ALIGN_SEPARATORS,   show::CODE_EDITOR_GROUP && show::CODE_ALIGN_SEPARATORS },
		{ settings_keys::CODE_EDIT_ON,            show::CODE_EDITOR_GROUP && show::CODE_EDIT_ON },
		{ settings_keys::DIAGNOSTIC_LOGGING,      show::SYSTEM_GROUP      && show::DIAGNOSTIC_LOGGING },
		{ settings_keys::LOG_FOLDER,              show::SYSTEM_GROUP      && show::LOG_FOLDER },
		{ settings_keys::LOG_FILE_NAME,           show::SYSTEM_GROUP      && show::LOG_FILE_NAME }
	};

	for ( const QPair<QString, bool>& expected : expectedFields )
	{
		QVERIFY2
		(
			( find_field ( groups, expected.first ) != nullptr ) == expected.second,
			qPrintable ( QStringLiteral ( "wrong visibility for %1" ).arg ( expected.first ) )
		);
	}

	// The bounded editors carry their bounds: SET-07's indent size is 1-8, and SET-06's name separator is 1-3 characters.

	if ( find_field ( groups, settings_keys::CODE_INDENT_SIZE ) != nullptr )
	{
		const SettingsField* const indentSize = find_field ( groups, settings_keys::CODE_INDENT_SIZE );

		QCOMPARE ( indentSize->minimumInteger, 1 );
		QCOMPARE ( indentSize->maximumInteger, 8 );
	}

	if ( find_field ( groups, settings_keys::TEXT_NAME_SEPARATOR ) != nullptr )
	{
		QCOMPARE ( find_field ( groups, settings_keys::TEXT_NAME_SEPARATOR )->maximumLength, 3 );
	}
}

void TestSettingsSchema::the_schema_defaults_match_the_readers_defaults ()
{
	// THE DRIFT GUARD. The schema states each default so the dialog has something to offer on a first run; the readers
	// state theirs so a view can be built without settings. Two statements of one value can part company, and the only
	// symptom would be a setting that silently changed the day the user first opened the dialog. So: apply the schema's
	// defaults to an empty store, and require every profile to come out exactly as an empty store already produces it.

	const SettingsStore* const emptyStore = settings.get ();

	const FormatProfile   defaultFormat = document_format_profile ( emptyStore );
	const TextViewProfile defaultText   = text_view_profile ( emptyStore );
	const ImportOptions   defaultImport = import_options ( emptyStore );

	const bool defaultFormClick = hands_over_caret_on_click ( emptyStore, settings_keys::FORM_EDIT_ON );
	const bool defaultCodeClick = hands_over_caret_on_click ( emptyStore, settings_keys::CODE_EDIT_ON );

	// SET-05 / EDIT-02. Not part of any profile -- a bare bool with its own reader -- so it needs naming here or the
	// drift guard simply would not see it.

	const bool defaultKeyEditing = key_editing_allowed ( emptyStore );

	QVERIFY ( defaultKeyEditing );                             // The stated default is Yes, not merely "whatever agrees".

	// SET-10 / FILE-12. Another bare bool with its own reader, and named here for the same reason.

	const bool defaultPageRules = print_page_rules ( emptyStore );

	QVERIFY ( defaultPageRules );                              // The stated default is Yes: a printed page carries its rules.

	// SET-03 / STYLE-02's pane corners. A third bare bool -- but the one default in the application whose two
	// statements are the SAME statement: the reader and the schema both name config::card::ROUNDED_TOP_CORNERS_DEFAULT,
	// so this cannot drift by construction. It is asserted anyway, because "cannot drift" is a claim about today's
	// code and the assertion is what makes it a claim about tomorrow's.

	const bool defaultRoundedCorners = rounded_pane_corners ( emptyStore );

	QVERIFY ( defaultRoundedCorners );                         // The stated default is Yes: the top corners are filleted.

	// Nothing is stored yet, so this writes the schema's idea of every default.

	const SettingsSnapshot snapshot ( settings_schema (), nullptr );

	snapshot.apply ( settings.get () );

	const FormatProfile   appliedFormat = document_format_profile ( settings.get () );
	const TextViewProfile appliedText   = text_view_profile ( settings.get () );

	QCOMPARE ( static_cast<int> ( appliedFormat.indent ), static_cast<int> ( defaultFormat.indent ) );
	QCOMPARE ( appliedFormat.indentSize,          defaultFormat.indentSize );
	QCOMPARE ( static_cast<int> ( appliedFormat.braceStyle ), static_cast<int> ( defaultFormat.braceStyle ) );
	QCOMPARE ( appliedFormat.alignNameSeparators, defaultFormat.alignNameSeparators );

	QCOMPARE ( appliedText.alignNameSeparators, defaultText.alignNameSeparators );
	QCOMPARE ( appliedText.nameSeparator,       defaultText.nameSeparator );
	QCOMPARE ( appliedText.includeObjectNames,  defaultText.includeObjectNames );
	QCOMPARE ( appliedText.includeArrayNames,   defaultText.includeArrayNames );
	QCOMPARE ( static_cast<int> ( appliedText.markdownListStyle ), static_cast<int> ( defaultText.markdownListStyle ) );
	QCOMPARE ( static_cast<int> ( appliedText.tableStyle ),        static_cast<int> ( defaultText.tableStyle ) );

	// The three Phase 11.7 fields, named here for the same reason as everything above: each is stated once by the schema
	// and once by a reader.

	QCOMPARE ( static_cast<int> ( appliedText.stringDisplay ), static_cast<int> ( defaultText.stringDisplay ) );
	QCOMPARE ( appliedText.wrapColumns > 0,                   defaultText.wrapColumns > 0 );
	QCOMPARE ( appliedText.blankLinesBetweenFields,           defaultText.blankLinesBetweenFields );

	QCOMPARE ( hands_over_caret_on_click ( settings.get (), settings_keys::FORM_EDIT_ON ), defaultFormClick );
	QCOMPARE ( hands_over_caret_on_click ( settings.get (), settings_keys::CODE_EDIT_ON ), defaultCodeClick );

	QCOMPARE ( key_editing_allowed   ( settings.get () ), defaultKeyEditing );
	QCOMPARE ( print_page_rules      ( settings.get () ), defaultPageRules );
	QCOMPARE ( rounded_pane_corners  ( settings.get () ), defaultRoundedCorners );

	// The two settings with no profile of their own, read directly by their consumers.

	QCOMPARE ( settings->value_bool ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, false ), true );
	QCOMPARE ( settings->value_string ( settings_keys::ON_DUPLICATE_KEYS, QString () ), settings_values::ON_DUPLICATE_KEEP_SILENTLY );
	QCOMPARE ( settings->value_bool ( settings_keys::DIAGNOSTIC_LOGGING, true ), false );

	// SET-06's table style default is Columnar, and it is the one default whose two statements DISAGREE by design: the
	// renderer's own TextViewProfile member default is vje_core's business, so the application's default is stated by
	// the reader and by the schema -- which is exactly the pair this case exists to keep in step.

	QCOMPARE ( static_cast<int> ( appliedText.tableStyle ), static_cast<int> ( TableStyle::Columnar ) );

	// And the XML import options are untouched by the dialog, so they must still read as their own defaults (SET-08).

	const ImportOptions appliedImport = import_options ( settings.get () );

	QCOMPARE ( static_cast<int> ( appliedImport.xmlStrategy ), static_cast<int> ( defaultImport.xmlStrategy ) );
	QCOMPARE ( appliedImport.xmlInferScalarTypes, defaultImport.xmlInferScalarTypes );
}

void TestSettingsSchema::a_null_store_reads_one_string_display_for_both_views ()
{
	// SET-03 is ONE setting for both views, so the two readers must answer alike at every input -- and a null store is a
	// real input, not a test artefact: it is the composition root's first run and every headless construction.
	//
	// text_view_profile returned early on a null store, leaving TextViewProfile's own member default in place. That
	// default is vje_core's IDENTITY transform (Decoded), deliberately not the application's preference, while the Form
	// View asks string_display_mode directly and got Escaped. Same node, two notations, on the one run where the user has
	// expressed no preference at all (2026-07-28 review).

	QCOMPARE ( static_cast<int> ( text_view_profile ( nullptr ).stringDisplay ),
	           static_cast<int> ( string_display_mode ( nullptr ) ) );

	QCOMPARE ( static_cast<int> ( string_display_mode ( nullptr ) ), static_cast<int> ( StringDisplay::Escaped ) );

	// And an EMPTY store is the same answer by a different route (D8: absent is not empty, but both mean "unstated").

	QCOMPARE ( static_cast<int> ( text_view_profile ( settings.get () ).stringDisplay ),
	           static_cast<int> ( string_display_mode ( settings.get () ) ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The snapshot
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsSchema::the_snapshot_seeds_from_the_store ()
{
	settings->set_string ( settings_keys::CODE_INDENT_KIND, settings_values::INDENT_TABS );
	settings->set_int    ( settings_keys::CODE_INDENT_SIZE, 8 );
	settings->set_bool   ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, false );

	const SettingsSnapshot snapshot ( settings_schema (), settings.get () );

	QCOMPARE ( snapshot.value_string ( settings_keys::CODE_INDENT_KIND ), settings_values::INDENT_TABS );
	QCOMPARE ( snapshot.value_int ( settings_keys::CODE_INDENT_SIZE ), 8 );
	QCOMPARE ( snapshot.value_bool ( settings_keys::CODE_SYNTAX_HIGHLIGHTING ), false );

	// A key with nothing stored falls back to the field's default rather than to a zero.

	QCOMPARE ( snapshot.value_string ( settings_keys::TEXT_NAME_SEPARATOR ), QStringLiteral ( ":" ) );
	QCOMPARE ( snapshot.value_bool ( settings_keys::TEXT_ALIGN_SEPARATORS ), true );
}

void TestSettingsSchema::edits_stay_out_of_the_store_until_apply ()
{
	SettingsSnapshot snapshot ( settings_schema (), settings.get () );

	snapshot.set_string ( settings_keys::CODE_INDENT_KIND, settings_values::INDENT_TABS );
	snapshot.set_int    ( settings_keys::CODE_INDENT_SIZE, 4 );
	snapshot.set_bool   ( settings_keys::TEXT_INCLUDE_ARRAYS, false );

	// Cancel is exactly this: the store never heard about any of it (SET-01).

	QVERIFY  ( !settings->contains ( settings_keys::CODE_INDENT_KIND ) );
	QVERIFY  ( !settings->contains ( settings_keys::CODE_INDENT_SIZE ) );
	QCOMPARE ( static_cast<int> ( document_format_profile ( settings.get () ).indent ), static_cast<int> ( IndentKind::Spaces ) );

	// OK is one pass over everything the dialog holds.

	snapshot.apply ( settings.get () );

	QCOMPARE ( static_cast<int> ( document_format_profile ( settings.get () ).indent ), static_cast<int> ( IndentKind::Tabs ) );
	QCOMPARE ( document_format_profile ( settings.get () ).indentSize, 4 );
	QCOMPARE ( text_view_profile ( settings.get () ).includeArrayNames, false );
}

void TestSettingsSchema::apply_reports_only_the_keys_that_changed ()
{
	// Seed the store with every default, so a following apply of an untouched snapshot changes nothing at all.

	SettingsSnapshot ( settings_schema (), nullptr ).apply ( settings.get () );

	SettingsSnapshot snapshot ( settings_schema (), settings.get () );

	QVERIFY ( snapshot.apply ( settings.get () ).isEmpty () );

	// One edit reports exactly one key. This is what lets the caller mirror the theme without re-applying a palette on
	// every visit to the dialog, and what keeps the views from re-rendering for settings nobody touched (SET-08).

	snapshot.set_string ( settings_keys::TEXT_TABLE_STYLE, settings_values::TABLE_STYLE_MARKDOWN );

	const QStringList changedKeys = snapshot.apply ( settings.get () );

	QCOMPARE ( changedKeys.size (), 1 );
	QCOMPARE ( changedKeys.first (), settings_keys::TEXT_TABLE_STYLE );
}

void TestSettingsSchema::the_system_group_fields_follow_the_logging_toggle ()
{
	const std::vector<SettingsGroup>& groups = settings_schema ();

	const SettingsField* const logging  = find_field ( groups, settings_keys::DIAGNOSTIC_LOGGING );
	const SettingsField* const folder   = find_field ( groups, settings_keys::LOG_FOLDER );
	const SettingsField* const fileName = find_field ( groups, settings_keys::LOG_FILE_NAME );

	SettingsSnapshot snapshot ( groups, settings.get () );

	// Off by default, so both dependents start inert (SET-09).

	QVERIFY ( snapshot.is_field_enabled ( *logging ) );
	QVERIFY ( !snapshot.is_field_enabled ( *folder ) );
	QVERIFY ( !snapshot.is_field_enabled ( *fileName ) );

	// The EDIT state, not the stored one: switching logging on in the dialog must enable its folder there and then,
	// before OK has written anything.

	snapshot.set_bool ( settings_keys::DIAGNOSTIC_LOGGING, true );

	QVERIFY ( snapshot.is_field_enabled ( *folder ) );
	QVERIFY ( snapshot.is_field_enabled ( *fileName ) );
	QVERIFY ( !settings->contains ( settings_keys::DIAGNOSTIC_LOGGING ) );
}

void TestSettingsSchema::the_toolbar_group_is_one_transfer_list_over_the_catalogue ()
{
	// Stand-ins for two catalogue commands and one entry with no action -- which can neither be labelled nor triggered
	// and is therefore skipped rather than offered as a blank row (SET-04).

	QAction saveAs ( QStringLiteral ( "&Save As..." ) );
	QAction undo ( QStringLiteral ( "&Undo" ) );

	const std::vector<ToolbarCommand> catalogue =
	{
		{ &saveAs, toolbar_names::SAVE_AS },
		{ &undo,   toolbar_names::UNDO },
		{ nullptr, QStringLiteral ( "phantom" ) }
	};

	const SettingsGroup group = toolbar_group ( catalogue );

	QCOMPARE ( group.title, QStringLiteral ( "Toolbar" ) );

	// ONE field, not one per button: the group is an ordered list now, and a transfer list is a single setting.

	QCOMPARE ( static_cast<int> ( group.fields.size () ), 1 );

	const SettingsField& field = group.fields [ 0 ];

	QCOMPARE ( static_cast<int> ( field.kind ), static_cast<int> ( SettingsFieldKind::TransferList ) );
	QCOMPARE ( field.key, settings_keys::TOOLBAR_LAYOUT );

	// It is a composite, so it takes the page rather than a row in the label/value columns (section 2.10).

	QVERIFY ( settings_field_spans_page ( field.kind ) );

	// The separator heads the options and is the repeatable one -- the only entry that may appear many times in a
	// layout, and the only one not consumed by being chosen.

	QCOMPARE ( field.repeatableValue, toolbar_names::SEPARATOR );
	QCOMPARE ( field.options.at ( 0 ).value, toolbar_names::SEPARATOR );

	// Then the catalogue in catalogue order, minus the actionless entry. Labels are the commands' own, stripped of the
	// mnemonic marker and the ellipsis, so both lists read the toolbar the way its tooltips do.

	QCOMPARE ( field.options.size (), 3 );
	QCOMPARE ( field.options.at ( 1 ).value, toolbar_names::SAVE_AS );
	QCOMPARE ( field.options.at ( 1 ).label, QStringLiteral ( "Save As" ) );
	QCOMPARE ( field.options.at ( 2 ).value, toolbar_names::UNDO );
	QCOMPARE ( field.options.at ( 2 ).label, QStringLiteral ( "Undo" ) );

	// The default is the SHIPPED layout, which is what Restore Defaults returns to -- deliberately not "the whole
	// catalogue in order", since the catalogue is the eligibility set and the layout is a curated subset of it.

	QCOMPARE ( field.defaultValue.toStringList (), default_toolbar_layout () );

	// And the group takes its place in the schema without disturbing the master-list order.

	const std::vector<SettingsGroup> assembled = settings_schema_with_toolbar ( catalogue );

	QCOMPARE ( static_cast<int> ( assembled.size () ), static_cast<int> ( settings_schema ().size () ) );
	QCOMPARE ( assembled [ 1 ].title, QStringLiteral ( "Toolbar" ) );
	QCOMPARE ( static_cast<int> ( assembled [ 1 ].fields.size () ), 1 );
}

void TestSettingsSchema::the_snapshot_tells_an_empty_layout_from_an_absent_one ()
{
	// The one place a QStringList setting needs more care than a scalar: an EMPTY stored layout is a legal value (SET-04
	// permits an empty toolbar) and must not be mistaken for "nothing stored, use the default". A value-with-fallback
	// read would silently restore the default toolbar every time the dialog opened.

	QAction undo ( QStringLiteral ( "&Undo" ) );

	const std::vector<ToolbarCommand> catalogue = { { &undo, toolbar_names::UNDO } };
	const std::vector<SettingsGroup>  groups    = { toolbar_group ( catalogue ) };

	// Nothing stored -> the shipped layout.

	QCOMPARE ( SettingsSnapshot ( groups, settings.get () ).value_string_list ( settings_keys::TOOLBAR_LAYOUT ),
	           default_toolbar_layout () );

	// An empty list stored -> an empty layout, kept.

	settings->set_string_list ( settings_keys::TOOLBAR_LAYOUT, QStringList () );

	QVERIFY ( settings->contains ( settings_keys::TOOLBAR_LAYOUT ) );
	QVERIFY ( SettingsSnapshot ( groups, settings.get () ).value_string_list ( settings_keys::TOOLBAR_LAYOUT ).isEmpty () );

	// And a real layout round-trips through the snapshot's apply as a list rather than as a string.

	const QStringList layout = { toolbar_names::UNDO, toolbar_names::SEPARATOR, toolbar_names::UNDO };

	SettingsSnapshot snapshot ( groups, settings.get () );

	snapshot.set_string_list ( settings_keys::TOOLBAR_LAYOUT, layout );

	QCOMPARE ( snapshot.apply ( settings.get () ), QStringList { settings_keys::TOOLBAR_LAYOUT } );
	QCOMPARE ( settings->value_string_list ( settings_keys::TOOLBAR_LAYOUT ), layout );
}

void TestSettingsSchema::restoring_defaults_reseeds_every_field ()
{
	// The Restore Defaults button's MECHANISM, tested where it is defined rather than through the dialog: reseeding the
	// snapshot from a NULL store. If that ever stopped meaning "every field at its schema default", the button would
	// quietly reset to something else, and no widget test would be needed to see it.

	// A store the user has customized across several groups, including one whose default is true (so the reset has to
	// move a value BACK UP, not merely clear it to a zero).

	settings->set_string      ( settings_keys::CODE_INDENT_KIND,        settings_values::INDENT_TABS );
	settings->set_int         ( settings_keys::CODE_INDENT_SIZE,        7 );
	settings->set_bool        ( settings_keys::TEXT_ALIGN_SEPARATORS,   false );
	settings->set_bool        ( settings_keys::FORM_ALLOW_KEY_EDITING,  false );
	settings->set_string_list ( settings_keys::TOOLBAR_LAYOUT,          QStringList () );

	SettingsSnapshot edited ( settings_schema (), settings.get () );

	QCOMPARE ( edited.value_string ( settings_keys::CODE_INDENT_KIND ), settings_values::INDENT_TABS );
	QCOMPARE ( edited.value_bool   ( settings_keys::FORM_ALLOW_KEY_EDITING ), false );

	// The reset itself.

	edited = SettingsSnapshot ( settings_schema (), nullptr );

	QCOMPARE ( edited.value_string ( settings_keys::CODE_INDENT_KIND ),       settings_values::INDENT_SPACES );
	QCOMPARE ( edited.value_int    ( settings_keys::CODE_INDENT_SIZE ),       2 );
	QCOMPARE ( edited.value_bool   ( settings_keys::TEXT_ALIGN_SEPARATORS ),  true );
	QCOMPARE ( edited.value_bool   ( settings_keys::FORM_ALLOW_KEY_EDITING ), true );

	// Cancel is still free after a reset -- the store has heard nothing yet, so the customized values stand.

	QCOMPARE ( settings->value_string ( settings_keys::CODE_INDENT_KIND, QString () ), settings_values::INDENT_TABS );
	QCOMPARE ( settings->value_bool   ( settings_keys::FORM_ALLOW_KEY_EDITING, true ), false );

	// And OK commits the reset in one pass, reporting exactly the keys it moved.

	const QStringList changedKeys = edited.apply ( settings.get () );

	QVERIFY ( changedKeys.contains ( settings_keys::CODE_INDENT_KIND ) );
	QVERIFY ( changedKeys.contains ( settings_keys::FORM_ALLOW_KEY_EDITING ) );

	QCOMPARE ( key_editing_allowed ( settings.get () ), true );
	QCOMPARE ( static_cast<int> ( document_format_profile ( settings.get () ).indent ), static_cast<int> ( IndentKind::Spaces ) );
}

//---------------------------------------------------------------------------------------------------------------------
// NFR-06 -- "confirm all preferences persist".
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsSchema::every_schema_setting_survives_a_restart ()
{
	// THE SCHEMA IS WALKED RATHER THAN A LIST BEING RESTATED HERE, and that is the point of the case: a settings key
	// added to the dialog in some later phase is covered by this the day it is added, with nobody remembering to come
	// back. A hand-written list would be a second statement of the schema, and the failure it is meant to catch -- a
	// setting the user can change but that does not come back -- is exactly the sort a forgotten list would miss.
	//
	// Every value written is DIFFERENT from the field's default, so a store that persisted nothing at all would read
	// its defaults back and fail rather than accidentally agreeing.

	const QString path = temporaryDirectory.path () + QStringLiteral ( "/persistence.json" );

	std::vector<SettingsGroup> groups = settings_schema ();

	// The Toolbar group is built over a catalogue rather than being one of the static groups, so it is added here with
	// a stand-in catalogue -- SET-04's ordered layout is the one setting stored as a LIST, and it would otherwise be
	// the one kind of value this case never exercised.

	QAction saveAs ( QStringLiteral ( "Save &As..." ) );
	QAction undo   ( QStringLiteral ( "&Undo" ) );

	const std::vector<ToolbarCommand> catalogue =
	{
		{ &saveAs, toolbar_names::SAVE_AS },
		{ &undo,   toolbar_names::UNDO }
	};

	groups.push_back ( toolbar_group ( catalogue ) );

	QMap<QString, QVariant> written;

	{
		SettingsStore store ( path );

		for ( const SettingsGroup& group : groups )
		{
			for ( const SettingsField& field : group.fields )
			{
				switch ( field.kind )
				{
					case SettingsFieldKind::YesNo:
					case SettingsFieldKind::CheckBox:
					{
						const bool flipped = !field.defaultValue.toBool ();

						store.set_bool ( field.key, flipped );

						written.insert ( field.key, flipped );

						break;
					}

					case SettingsFieldKind::Integer:
					{
						// Somewhere inside the declared range that is not the default.

						const int other = ( field.defaultValue.toInt () == field.maximumInteger )
						                ? field.minimumInteger
						                : field.maximumInteger;

						// The same guard the Choice branch carries, and for the same reason it was added there: a field
						// whose range holds only its own default would write back the value already stored and pass
						// while proving nothing. No such field exists today; the assertion is what makes that a fact
						// rather than an assumption about every field added later.

						QVERIFY2
						(
							other != field.defaultValue.toInt (),
							qPrintable ( QStringLiteral ( "%1's range offers only its default, so this case proves nothing" )
							             .arg ( field.key ) )
						);

						store.set_int ( field.key, other );

						written.insert ( field.key, other );

						break;
					}

					case SettingsFieldKind::Choice:
					{
						// The first option that is NOT the default. Written as a search rather than "the last one"
						// because the last one is the default for at least one field (formView.editOn defaults to
						// Double click, which is the last of the two) -- caught by the assertion below when this case
						// was first written that way, which is why the assertion stays.

						QVERIFY ( !field.options.isEmpty () );

						QString other;

						for ( const SettingsOption& option : field.options )
						{
							if ( option.value != field.defaultValue.toString () )
							{
								other = option.value;

								break;
							}
						}

						QVERIFY2
						(
							!other.isEmpty (),
							qPrintable ( QStringLiteral ( "%1 offers only its default, so this case proves nothing" )
							             .arg ( field.key ) )
						);

						store.set_string ( field.key, other );

						written.insert ( field.key, other );

						break;
					}

					case SettingsFieldKind::ShortText:
					case SettingsFieldKind::Folder:
					{
						const QString other = QStringLiteral ( "x" );

						store.set_string ( field.key, other );

						written.insert ( field.key, other );

						break;
					}

					case SettingsFieldKind::TransferList:
					{
						// Reversed, so both membership and ORDER have to come back (SET-04's whole point).

						QStringList reversed = field.defaultValue.toStringList ();

						std::reverse ( reversed.begin (), reversed.end () );

						store.set_string_list ( field.key, reversed );

						written.insert ( field.key, reversed );

						break;
					}
				}
			}
		}
	}

	QVERIFY ( !written.isEmpty () );

	// A second store over the same file, which is what a restart is.

	SettingsStore reopened ( path );

	for ( auto entry = written.constBegin (); entry != written.constEnd (); ++entry )
	{
		QVERIFY2
		(
			reopened.contains ( entry.key () ),
			qPrintable ( QStringLiteral ( "%1 did not survive a restart at all" ).arg ( entry.key () ) )
		);

		const QVariant expected = entry.value ();

		if ( expected.typeId () == QMetaType::QStringList )
		{
			QCOMPARE ( reopened.value_string_list ( entry.key () ), expected.toStringList () );
		}
		else if ( expected.typeId () == QMetaType::Bool )
		{
			QCOMPARE ( reopened.value_bool ( entry.key (), !expected.toBool () ), expected.toBool () );
		}
		else if ( expected.typeId () == QMetaType::Int )
		{
			QCOMPARE ( reopened.value_int ( entry.key (), expected.toInt () - 1 ), expected.toInt () );
		}
		else
		{
			QCOMPARE ( reopened.value_string ( entry.key (), QString () ), expected.toString () );
		}
	}
}

void TestSettingsSchema::the_session_state_keys_survive_a_restart ()
{
	// The other half of NFR-06, and the half that CANNOT be derived from the schema: these are persisted UI state with
	// no Settings-dialog surface, written by the window, the file controller and the XML import dialog. They are listed
	// by hand because there is nothing to walk -- which is stated here so the next person does not take the list for an
	// oversight and try to generate it.

	const QString path = temporaryDirectory.path () + QStringLiteral ( "/session.json" );

	const QByteArray  geometry = QByteArrayLiteral ( "\x01\x02\x03 geometry" );
	const QByteArray  state    = QByteArrayLiteral ( "\x04\x05\x06 state" );
	const QStringList sizes    = { QStringLiteral ( "280" ), QStringLiteral ( "744" ) };
	const QStringList recent   = { QStringLiteral ( "a.json" ), QStringLiteral ( "b.json" ) };

	{
		SettingsStore store ( path );

		store.set_bytes       ( settings_keys::WINDOW_GEOMETRY,  geometry );
		store.set_bytes       ( settings_keys::WINDOW_STATE,     state );
		store.set_string_list ( settings_keys::SPLITTER_SIZES,   sizes );
		store.set_string_list ( settings_keys::RECENT_FILES,     recent );

		store.set_string ( settings_keys::XML_IMPORT_STRATEGY,    QStringLiteral ( "BadgerFish" ) );
		store.set_bool   ( settings_keys::XML_INFER_SCALAR_TYPES, true );

		// Stored EMPTY rather than absent once the user has been through the dialog -- the D8 distinction between a
		// value and a first run, so the empty string has to survive as a value.

		store.set_string ( settings_keys::XML_TEXT_VALUE_KEY, QString () );
	}

	SettingsStore reopened ( path );

	QCOMPARE ( reopened.value_bytes       ( settings_keys::WINDOW_GEOMETRY ), geometry );
	QCOMPARE ( reopened.value_bytes       ( settings_keys::WINDOW_STATE ),    state );
	QCOMPARE ( reopened.value_string_list ( settings_keys::SPLITTER_SIZES ),  sizes );
	QCOMPARE ( reopened.value_string_list ( settings_keys::RECENT_FILES ),    recent );

	QCOMPARE ( reopened.value_string ( settings_keys::XML_IMPORT_STRATEGY, QString () ), QStringLiteral ( "BadgerFish" ) );
	QCOMPARE ( reopened.value_bool   ( settings_keys::XML_INFER_SCALAR_TYPES, false ),   true );

	QVERIFY  ( reopened.contains     ( settings_keys::XML_TEXT_VALUE_KEY ) );
	QCOMPARE ( reopened.value_string ( settings_keys::XML_TEXT_VALUE_KEY, QStringLiteral ( "fallback" ) ), QString () );
}

QTEST_MAIN ( TestSettingsSchema )

#include "tst_settings_schema.moc"

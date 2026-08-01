//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   settings_schema -- the Settings dialog stated as DATA (SET-01..09), plus the edit buffer that makes its OK / Cancel
//   an all-or-nothing operation.
//
//   WHY A SCHEMA RATHER THAN SIX HAND-BUILT PAGES. SET-02 asks for the group set to be extensible -- "adding a group is
//   one new detail page plus one master-list entry" -- and that promise is only real if a page is not code. Here a group
//   is a title and a list of fields, a field is a kind plus a key plus a default, and SettingsDialog renders whatever it
//   is handed. Adding a setting is one entry in this table; the dialog does not change at all.
//
//   It also makes the dialog TESTABLE without a dialog. The parts worth pinning -- the group order of SET-02, every
//   documented default, the dependency that greys the log folder while logging is off (SET-09), and the promise that
//   Cancel writes nothing -- are all properties of the schema and the snapshot, so they are checked headlessly.
//
//   ONE THING IS DELIBERATELY NOT HERE. The Toolbar group (SET-04) is one check box per toolbar BUTTON, and the toolbar
//   is the window's. A second list of buttons here would be a second opinion about what the toolbar contains, so the
//   window builds that group from its own actions through toolbar_group() and hands it to the dialog with the rest.
//
//   DEFAULTS ARE STATED TWICE, and that is watched rather than tolerated: the readers (settings_profiles, the views,
//   ThemeService, DiagnosticLog) each carry the default they read with, and this table carries the default the dialog
//   offers. tst_settings_schema asserts the two agree by applying the schema's defaults to an empty store and comparing
//   the resulting profiles against what an empty store already produces -- so a drift is a failing test, not a surprise
//   in the field.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/toolbar_catalogue.hpp"

#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

class QAction;

namespace vje
{
	class SettingsStore;

	//-----------------------------------------------------------------------------------------------------------------
	// How a field is edited. The three value-carrying kinds map onto the store's three types; YesNo and CheckBox are
	// both booleans and differ only in presentation -- section 2.10's sketch shows the group settings as Yes / No
	// dropdowns, while the Toolbar group is a check-box list (SET-04).
	//-----------------------------------------------------------------------------------------------------------------

	enum class SettingsFieldKind
	{
		Choice,                                                // Dropdown over a fixed option list; stores a string.
		YesNo,                                                 // Yes / No dropdown; stores a bool.
		CheckBox,                                              // Check box; stores a bool.
		Integer,                                               // Bounded spin box.
		ShortText,                                             // Line edit with a length cap.
		Folder,                                                // Line edit plus a Browse folder picker (SET-09).
		TransferList                                           // Two lists plus order; stores an ordered string list.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Is this kind's editor a composite control rather than a single field? A composite spans the whole width of the
	// detail page and carries its own captions, instead of sitting in the value column beside a label (section 2.10).
	//
	// Asked of the KIND rather than carried as a flag on the field, because it is a property of the editor and not a
	// per-setting choice -- a transfer list is a page wherever it appears.
	//-----------------------------------------------------------------------------------------------------------------

	bool settings_field_spans_page ( SettingsFieldKind kind );

	//-----------------------------------------------------------------------------------------------------------------
	// One option of a Choice or TransferList field: what the user sees, and what is stored (settings_values, so the
	// stored spelling is never invented at a call site).
	//
	// The icon is optional and currently used only by the TransferList, whose rows carry the command's own toolbar
	// glyph so the two lists are scanned the way the toolbar is (SET-04). Carried on the option rather than resolved by
	// the dialog, so SettingsDialog still needs no IconLibrary and still knows no individual setting.
	//-----------------------------------------------------------------------------------------------------------------

	struct SettingsOption
	{
		QString label;
		QString value;

		// Default-initialized in place, so the many { label, value } option lists stay two-element aggregates rather
		// than each having to name an icon they do not have (-Wmissing-field-initializers is an error in CI).

		QIcon icon = {};
	};

	//-----------------------------------------------------------------------------------------------------------------
	// One setting.
	//-----------------------------------------------------------------------------------------------------------------

	struct SettingsField
	{
		SettingsFieldKind kind = SettingsFieldKind::YesNo;
		QString           key;                                 // A settings_keys entry (or a toolbar visibility key).
		QString           label;

		QList<SettingsOption> options;                         // Choice and TransferList.

		QVariant defaultValue;                                 // Typed to match the kind (a QStringList for TransferList).

		int minimumInteger = 0;                                // Integer only.
		int maximumInteger = 0;
		int maximumLength  = 0;                                // ShortText only (0 => unbounded).

		QString placeholder;                                   // ShortText / Folder: shown when the value is empty.

		// TransferList only. The two captions, and the one option value that is REPEATABLE -- never consumed by being
		// chosen, never returned by being removed, and permitted to appear many times in the result (SET-04's
		// separator). Empty means every option is unique, which is the ordinary case.

		QString chosenListLabel;
		QString availableListLabel;
		QString repeatableValue;

		// The field is editable only while this boolean setting is on. SET-09's log folder and file name, which are inert
		// while diagnostic logging is off. Empty means always editable.
		//
		// A disabled field greys out as a WHOLE ROW, its text label included (SET-01b) -- which is SettingsDialog's job
		// rather than this table's, but it is the reason the dependency is declared here rather than being wired at
		// each dependent field's editor.

		QString enabledByKey;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// One master-list entry and its detail page.
	//-----------------------------------------------------------------------------------------------------------------

	struct SettingsGroup
	{
		QString                    title;
		std::vector<SettingsField> fields;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The schema.
	//-----------------------------------------------------------------------------------------------------------------

	// The static groups in SET-02's master-list order: General, Toolbar (empty here -- see the header), Form View
	// Editor, Text View, Code Editor, System (last).

	const std::vector<SettingsGroup>& settings_schema ();

	// The Toolbar group (SET-04): ONE TransferList field whose options are the separator followed by the catalogue in
	// catalogue order, and whose default is the shipped layout. Commands with no action are skipped.
	//
	// The CURRENT layout needs no parameter. The snapshot reads it from the store, and the one case the store cannot
	// answer -- a first run, or a settings file still carrying the superseded per-button keys -- has already been
	// resolved by the window through stored_toolbar_layout() before any dialog can open.

	SettingsGroup toolbar_group ( const std::vector<ToolbarCommand>& catalogue );

	// The schema with the Toolbar group filled in -- what the dialog is actually handed.

	std::vector<SettingsGroup> settings_schema_with_toolbar ( const std::vector<ToolbarCommand>& catalogue );

	//*****************************************************************************************************************
	// Class: SettingsSnapshot -- the dialog's edit buffer (SET-01).
	//*****************************************************************************************************************

	class SettingsSnapshot
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Seed every field in groups from the store, falling back to the field's default. A null store yields the
		// defaults, which is the state a first run is in.

		SettingsSnapshot ( const std::vector<SettingsGroup>& groups, const SettingsStore* store );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QString     value_string      ( const QString& key ) const;
		bool        value_bool        ( const QString& key ) const;
		int         value_int         ( const QString& key ) const;
		QStringList value_string_list ( const QString& key ) const;

		// Is this field editable in the CURRENT edit state? Asked of the snapshot rather than the store, so turning
		// logging on in the dialog enables its folder immediately -- before OK has written anything (SET-09).

		bool is_field_enabled ( const SettingsField& field ) const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_string      ( const QString& key, const QString& value );
		void set_bool        ( const QString& key, bool value );
		void set_int         ( const QString& key, int value );
		void set_string_list ( const QString& key, const QStringList& value );

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Write every held value to the store (SET-01 / SET-08: OK applies and persists all edits as one operation). The
		// store dedupes a value that has not changed, so untouched settings neither rewrite the file nor signal -- which
		// is what keeps the views from re-rendering for settings the user never opened.
		//
		// Returns the keys that actually changed, most useful to the caller that has to mirror one of them (the theme).

		QStringList apply ( SettingsStore* store ) const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QHash<QString, QVariant>          values;
		QHash<QString, SettingsFieldKind> kinds;               // So apply() writes each key back as its own TYPE.
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   settings_schema implementation -- the table itself, and the snapshot's seed / apply.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/settings_schema.hpp"

#include "AppConfig.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <platform/explorer_integration.hpp>

#include <QAction>
#include <QObject>

#include <utility>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Field builders. One per kind, so the table below reads as a list of settings rather than a list of struct
		// initializers -- and so a new field cannot forget a member.
		//-------------------------------------------------------------------------------------------------------------

		SettingsField choice_field
		(
			const QString&               key,
			const QString&               label,
			const QList<SettingsOption>& options,
			const QString&               defaultValue,
			const QString&               description
		)
		{
			SettingsField field;

			field.kind         = SettingsFieldKind::Choice;
			field.key          = key;
			field.label        = label;
			field.description  = description;
			field.options      = options;
			field.defaultValue = defaultValue;

			return field;
		}

		SettingsField yes_no_field ( const QString& key, const QString& label, bool defaultValue, const QString& description )
		{
			SettingsField field;

			field.kind         = SettingsFieldKind::YesNo;
			field.key          = key;
			field.label        = label;
			field.description  = description;
			field.defaultValue = defaultValue;

			return field;
		}

		SettingsField integer_field
		(
			const QString& key,
			const QString& label,
			int            defaultValue,
			int            minimum,
			int            maximum,
			const QString& description
		)
		{
			SettingsField field;

			field.kind           = SettingsFieldKind::Integer;
			field.key            = key;
			field.label          = label;
			field.description    = description;
			field.defaultValue   = defaultValue;
			field.minimumInteger = minimum;
			field.maximumInteger = maximum;

			return field;
		}

		SettingsField short_text_field
		(
			const QString& key,
			const QString& label,
			const QString& defaultValue,
			int            maximumLength,
			const QString& description
		)
		{
			SettingsField field;

			field.kind          = SettingsFieldKind::ShortText;
			field.key           = key;
			field.label         = label;
			field.description   = description;
			field.defaultValue  = defaultValue;
			field.maximumLength = maximumLength;

			return field;
		}

		//-------------------------------------------------------------------------------------------------------------
		// The table itself. Assembled imperatively rather than as one initializer, so every row can be gated on its
		// AppConfig switch (config::settings_dialog::show) at the point the row is stated -- one place to look for both
		// "what does the dialog offer?" and "is this row switched on?".
		//-------------------------------------------------------------------------------------------------------------

		void add_field ( std::vector<SettingsField>& fields, bool visible, const SettingsField& field )
		{
			if ( visible )
			{
				fields.push_back ( field );
			}
		}

		// A group box joins its page only when it has something to show (SET-01c) -- SET-01's rule for a group, applied
		// one level down. It is what removes the Windows Explorer box on Linux, where its one row is absent: no platform
		// question is asked here, or anywhere in the renderer.

		void add_section ( std::vector<SettingsSection>& sections, const QString& title, std::vector<SettingsField> fields )
		{
			if ( fields.empty () )
			{
				return;
			}

			SettingsSection section;

			section.title  = title;
			section.fields = std::move ( fields );

			sections.push_back ( std::move ( section ) );
		}

		// A group joins the master list only when it is switched on AND has something to show, so the list never offers
		// an empty page. allowEmpty is the Toolbar group's exception: it is deliberately empty here because its fields
		// are the window's own buttons (SET-04), filled in by settings_schema_with_toolbar.

		void add_group
		(
			std::vector<SettingsGroup>&  groups,
			bool                         visible,
			const QString&               title,
			std::vector<SettingsSection> sections,
			bool                         allowEmpty = false
		)
		{
			if ( !visible || ( sections.empty () && !allowEmpty ) )
			{
				return;
			}

			SettingsGroup group;

			group.title    = title;
			group.sections = std::move ( sections );

			groups.push_back ( std::move ( group ) );
		}

		std::vector<SettingsGroup> build_schema ()
		{
			namespace show = config::settings_dialog::show;

			std::vector<SettingsGroup> groups;

			//---------------------------------------------------------------------------------------------------------
			// General (SET-03).
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> applicationUpdates;

			add_field
			(
				applicationUpdates,
				show::CHECK_UPDATES,
				yes_no_field
				(
					settings_keys::CHECK_UPDATES,
					QObject::tr ( "Check for updates automatically" ),
					true,
					QObject::tr ( "Look for a newer version of VJE in the background. Nothing is sent but the request for the update feed." )
				)
			);

			std::vector<SettingsField> duplicateKeys;

			add_field
			(
				duplicateKeys,
				show::ON_DUPLICATE_KEYS,
				choice_field
				(
					settings_keys::ON_DUPLICATE_KEYS,
					QObject::tr ( "On duplicate keys when loading" ),
					{
						{ QObject::tr ( "Keep silently" ), settings_values::ON_DUPLICATE_KEEP_SILENTLY },
						{ QObject::tr ( "Keep and warn" ), settings_values::ON_DUPLICATE_KEEP_AND_WARN }
					},
					settings_values::ON_DUPLICATE_KEEP_SILENTLY,
					QObject::tr ( "What happens when a file you open has an object carrying the same key twice. Both choices keep every member; Keep and warn also says so in the status bar." )
				)
			);

			// SET-03a, beside the setting it completes: the row above says what LOADING a file with duplicates does,
			// and this one says whether an EDIT may create them.
			//
			// Named "Allow duplicate keys" on the user's direction (2026-08-28), over an earlier "Allow edits to
			// create duplicate keys" that spelled the distinction out. The short name reads as the general policy it
			// has in fact become -- it governs every edit path AND, since the read-only rule lifted, whether a
			// duplicate member can be worked with at all -- and its neighbour's own label already says "when loading",
			// so the pair still divides cleanly.

			add_field
			(
				duplicateKeys,
				show::ALLOW_DUPLICATE_KEYS,
				yes_no_field
				(
					settings_keys::ALLOW_DUPLICATE_KEYS,
					QObject::tr ( "Allow duplicate keys" ),
					false,
					QObject::tr ( "Whether an edit \xE2\x80\x94 a rename, an add, a paste or a Code View commit \xE2\x80\x94 may give an object a second member with a key it already has." )
				)
			);

			// SET-01c's boxes. The two duplicate-key rows share one, which is the pairing the comment above describes:
			// what loading does, and what an edit may do.

			std::vector<SettingsSection> general;

			add_section ( general, QObject::tr ( "Application Updates" ), std::move ( applicationUpdates ) );
			add_section ( general, QObject::tr ( "Duplicate Keys" ),      std::move ( duplicateKeys ) );

			add_group ( groups, show::GENERAL_GROUP, QObject::tr ( "General" ), std::move ( general ) );

			//---------------------------------------------------------------------------------------------------------
			// Appearance (SET-11). The settings that decide how the application LOOKS, which had been scattered between
			// General and the view groups. Theme is the SAME setting as View > Theme, mirrored -- the dialog routes it
			// through ThemeService so the palette follows the value (section 2.9).
			//
			// THE STORED KEYS DID NOT MOVE with the settings: they are still general.theme and general.stringDisplay. A
			// group is a PRESENTATION of settings, and renaming a key to match the page it is drawn on would be a
			// migration -- every reader edited, every user's settings file rewritten -- bought for a namespace nobody
			// sees. What moved is one line of schema each.
			//
			// Settings that belong to one VIEW stay with their view: the Code Editor's syntax highlighting decides how
			// that editor looks and means nothing beside a global theme.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> themeAndStyle;

			add_field
			(
				themeAndStyle,
				show::THEME,
				choice_field
				(
					settings_keys::THEME,
					QObject::tr ( "Theme" ),
					{
						{ QObject::tr ( "Light" ),  settings_values::THEME_LIGHT },
						{ QObject::tr ( "Dark" ),   settings_values::THEME_DARK },
						{ QObject::tr ( "System" ), settings_values::THEME_SYSTEM }
					},
					settings_values::THEME_LIGHT,
					QObject::tr ( "Light, Dark, or whatever the operating system is set to. The same choice as View \xE2\x96\xB8 Theme." )
				)
			);

			// SET-12's Interface style. Supersedes SET-03's "Rounded pane corners", which asked the same question of
			// the cards alone and left the controls with no way to answer it.
			//
			// The default is named rather than written out: config::appearance::DEFAULT_INTERFACE_STYLE is the one
			// statement of it, shared with the reader in settings_profiles, so the pair the drift guard exists to watch
			// is a single constant.

			add_field
			(
				themeAndStyle,
				show::INTERFACE_STYLE,
				choice_field
				(
					settings_keys::INTERFACE_STYLE,
					QObject::tr ( "Interface style" ),
					{
						{ QObject::tr ( "Classic" ), settings_values::INTERFACE_STYLE_CLASSIC },
						{ QObject::tr ( "Fluent" ),  settings_values::INTERFACE_STYLE_FLUENT }
					},
					( config::appearance::DEFAULT_INTERFACE_STYLE == config::appearance::InterfaceStyle::Classic )
						? settings_values::INTERFACE_STYLE_CLASSIC
						: settings_values::INTERFACE_STYLE_FLUENT,
					QObject::tr ( "Fluent rounds the panes and controls and draws Fluent icons; Classic squares them and draws VJE's own icons." )
				)
			);

			// SET-11's String display. One setting for the Form View and the Text View, which is why it sits here
			// rather than in either view's group.

			std::vector<SettingsField> stringFormatting;

			add_field
			(
				stringFormatting,
				show::STRING_DISPLAY,
				choice_field
				(
					settings_keys::STRING_DISPLAY,
					QObject::tr ( "String display" ),
					{
						{ QObject::tr ( "Escaped" ),   settings_values::STRING_DISPLAY_ESCAPED },
						{ QObject::tr ( "Decoded" ),   settings_values::STRING_DISPLAY_DECODED },
						{ QObject::tr ( "Flattened" ), settings_values::STRING_DISPLAY_FLATTENED }
					},
					settings_values::STRING_DISPLAY_ESCAPED,
					QObject::tr ( "How characters such as tabs and line breaks appear in string values, in the Form View and the Text View: as escapes, as themselves, or removed." )
				)
			);

			// SET-14, in a box of its own named after the pane it changes (STYLE-13): where TREE-10's unsaved-change
			// dots go. The default is config::tree's, named here and by the reader so the drift guard watches one
			// constant.

			std::vector<SettingsField> explorer;

			add_field
			(
				explorer,
				show::MARK_UNSAVED_CHANGES,
				choice_field
				(
					settings_keys::MARK_UNSAVED_CHANGES,
					QObject::tr ( "Mark unsaved changes" ),
					{
						{ QObject::tr ( "Changed nodes and ancestors" ), settings_values::CHANGE_MARKS_CHANGED_NODES_AND_ANCESTORS },
						{ QObject::tr ( "File node only" ),              settings_values::CHANGE_MARKS_FILE_NODE_ONLY }
					},
					( config::tree::DEFAULT_CHANGE_MARK_SCOPE == config::tree::ChangeMarkScope::FileNodeOnly )
						? settings_values::CHANGE_MARKS_FILE_NODE_ONLY
						: settings_values::CHANGE_MARKS_CHANGED_NODES_AND_ANCESTORS,
					QObject::tr ( "Where the tree puts a dot for an unsaved change: on every changed node and each node above it, or on the file node alone." )
				)
			);

			// SET-14a, the dot's colour: a hexadecimal value, typed or picked. Stored as the DARK theme's colour; the dialog
			// shows and takes it in the theme showing when it opens, and the Light theme inverts its lightness
			// (style/theme_colour). The default is config::tree's, named here and by the reader.

			SettingsField changeMarkColour;

			changeMarkColour.kind         = SettingsFieldKind::Colour;
			changeMarkColour.key          = settings_keys::CHANGE_MARK_COLOUR;
			changeMarkColour.label        = QObject::tr ( "Unsaved change color" );
			changeMarkColour.description  = QObject::tr ( "The color of the dot that marks an unsaved change, as a hex value such as #808080 \xE2\x80\x94 typed, or picked with Choose. It is the color in the theme showing now; the other theme draws it with its lightness inverted." );
			changeMarkColour.defaultValue = QString::fromLatin1 ( config::tree::DEFAULT_CHANGE_MARK_COLOUR_DARK );

			add_field ( explorer, show::CHANGE_MARK_COLOUR, changeMarkColour );

			std::vector<SettingsSection> appearance;

			add_section ( appearance, QObject::tr ( "Theme & Style" ), std::move ( themeAndStyle ) );
			add_section ( appearance, QObject::tr ( "Formatting" ),    std::move ( stringFormatting ) );
			add_section ( appearance, QObject::tr ( "Explorer" ),      std::move ( explorer ) );

			add_group ( groups, show::APPEARANCE_GROUP, QObject::tr ( "Appearance" ), std::move ( appearance ) );

			//---------------------------------------------------------------------------------------------------------
			// Toolbar (SET-04). Present here so the master list keeps SET-02's order; its fields come from the toolbar.
			//---------------------------------------------------------------------------------------------------------

			add_group ( groups, show::TOOLBAR_GROUP, QObject::tr ( "Toolbar" ), {}, true );

			//---------------------------------------------------------------------------------------------------------
			// Form View Editor (SET-05).
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> formattingAndBehavior;

			add_field
			(
				formattingAndBehavior,
				show::FORM_EDIT_ON,
				choice_field
				(
					settings_keys::FORM_EDIT_ON,
					QObject::tr ( "Edit on" ),
					{
						{ QObject::tr ( "Single click" ), settings_values::EDIT_ON_SINGLE_CLICK },
						{ QObject::tr ( "Double click" ), settings_values::EDIT_ON_DOUBLE_CLICK }
					},
					settings_values::EDIT_ON_DOUBLE_CLICK,
					QObject::tr ( "Whether one click on a node in the tree starts editing it in the Form View, or only selects it until a second click." )
				)
			);

			add_field
			(
				formattingAndBehavior,
				show::FORM_ALLOW_JAGGED_PASTE,
				yes_no_field ( settings_keys::FORM_ALLOW_JAGGED_PASTE, QObject::tr ( "Allow jagged-array paste" ), false, QObject::tr ( "Whether objects that do not match the table's columns may be pasted into it, after a warning. Off, such a paste is refused." ) )
			);

			// EDIT-02. Default Yes: the editor is fully editable unless the user asks otherwise. Switching it off reaches
			// BOTH routes to a rename -- the key column and the Rename Key command -- through key_editing_allowed().

			add_field
			(
				formattingAndBehavior,
				show::FORM_ALLOW_KEY_EDITING,
				yes_no_field ( settings_keys::FORM_ALLOW_KEY_EDITING, QObject::tr ( "Allow key editing" ), true, QObject::tr ( "Whether an existing key can be renamed \xE2\x80\x94 in the Form View's key column and with Document \xE2\x96\xB8 Rename Key." ) )
			);

			add_field
			(
				formattingAndBehavior,
				show::FORM_WRAP_STRINGS,
				yes_no_field ( settings_keys::FORM_WRAP_STRINGS, QObject::tr ( "Wrap strings" ), false, QObject::tr ( "Show a long value across several lines in the object form instead of cutting it short. Tables keep one line per row." ) )
			);

			std::vector<SettingsSection> formView;

			add_section ( formView, QObject::tr ( "Formatting & Behavior" ), std::move ( formattingAndBehavior ) );

			add_group ( groups, show::FORM_VIEW_GROUP, QObject::tr ( "Form View Editor" ), std::move ( formView ) );

			//---------------------------------------------------------------------------------------------------------
			// Text View (SET-06). The rendering options of EDITOR-06, in the spec's table order.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> textFormatting;

			add_field ( textFormatting, show::TEXT_WRAP_STRINGS,     yes_no_field     ( settings_keys::TEXT_WRAP_STRINGS,     QObject::tr ( "Wrap strings" ), false, QObject::tr ( "Wrap a long value to the width of the view, with a hanging indent, instead of letting it run off the right edge." ) ) );
			add_field ( textFormatting, show::TEXT_BLANK_LINES,      integer_field    ( settings_keys::TEXT_BLANK_LINES,      QObject::tr ( "Blank lines between fields" ), 0, 0, 10, QObject::tr ( "How many empty lines separate one field from the next." ) ) );
			add_field ( textFormatting, show::TEXT_ALIGN_SEPARATORS, yes_no_field     ( settings_keys::TEXT_ALIGN_SEPARATORS, QObject::tr ( "Align name separators" ), true, QObject::tr ( "Pad names so the separators after them line up in one column." ) ) );
			add_field ( textFormatting, show::TEXT_NAME_SEPARATOR,   short_text_field ( settings_keys::TEXT_NAME_SEPARATOR,   QObject::tr ( "Name separator" ), QStringLiteral ( ":" ), 3, QObject::tr ( "The characters written between each name and its value." ) ) );
			add_field ( textFormatting, show::TEXT_INCLUDE_OBJECTS,  yes_no_field     ( settings_keys::TEXT_INCLUDE_OBJECTS,  QObject::tr ( "Include object names" ), true, QObject::tr ( "Whether a child object gets a row of its own in the listing." ) ) );
			add_field ( textFormatting, show::TEXT_INCLUDE_ARRAYS,   yes_no_field     ( settings_keys::TEXT_INCLUDE_ARRAYS,   QObject::tr ( "Include array names" ), true, QObject::tr ( "Whether a child array gets a row of its own in the listing." ) ) );

			add_field
			(
				textFormatting,
				show::TEXT_MARKDOWN_STYLE,
				choice_field
				(
					settings_keys::TEXT_MARKDOWN_STYLE,
					QObject::tr ( "Markdown list style" ),
					{
						{ QObject::tr ( "None" ),  settings_values::MARKDOWN_STYLE_NONE },
						{ QObject::tr ( "List" ),  settings_values::MARKDOWN_STYLE_LIST },
						{ QObject::tr ( "Table" ), settings_values::MARKDOWN_STYLE_TABLE }
					},
					settings_values::MARKDOWN_STYLE_NONE,
					QObject::tr ( "Write the listing as plain rows, as a Markdown bulleted list, or as a Markdown table, ready to copy." )
				)
			);

			add_field
			(
				textFormatting,
				show::TEXT_TABLE_STYLE,
				choice_field
				(
					settings_keys::TEXT_TABLE_STYLE,
					QObject::tr ( "Table style" ),
					{
						{ QObject::tr ( "Academic" ),    settings_values::TABLE_STYLE_ACADEMIC },
						{ QObject::tr ( "Compact" ),     settings_values::TABLE_STYLE_COMPACT },
						{ QObject::tr ( "Columnar" ),    settings_values::TABLE_STYLE_COLUMNAR },
						{ QObject::tr ( "Spreadsheet" ), settings_values::TABLE_STYLE_SPREADSHEET },
						{ QObject::tr ( "Minimal" ),     settings_values::TABLE_STYLE_MINIMAL },
						{ QObject::tr ( "Markdown" ),    settings_values::TABLE_STYLE_MARKDOWN },
						{ QObject::tr ( "CSV" ),         settings_values::TABLE_STYLE_CSV },
						{ QObject::tr ( "TSV" ),         settings_values::TABLE_STYLE_TSV }
					},
					settings_values::TABLE_STYLE_COLUMNAR,
					QObject::tr ( "How an array is drawn when it is shown as a table." )
				)
			);

			std::vector<SettingsSection> textView;

			add_section ( textView, QObject::tr ( "Formatting" ), std::move ( textFormatting ) );

			add_group ( groups, show::TEXT_VIEW_GROUP, QObject::tr ( "Text View" ), std::move ( textView ) );

			//---------------------------------------------------------------------------------------------------------
			// Code Editor (SET-07). The first four ARE the document format profile, shared verbatim with File > Save
			// (FILE-03) -- so editing them here changes what a save writes, and re-formats the Code View's text the
			// moment OK commits. That is the contract, not a side effect.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> indentation;

			add_field
			(
				indentation,
				show::CODE_INDENT_KIND,
				choice_field
				(
					settings_keys::CODE_INDENT_KIND,
					QObject::tr ( "Indentation" ),
					{
						{ QObject::tr ( "Spaces" ), settings_values::INDENT_SPACES },
						{ QObject::tr ( "Tabs" ),   settings_values::INDENT_TABS }
					},
					settings_values::INDENT_SPACES,
					QObject::tr ( "Indent with spaces or with tabs \xE2\x80\x94 in the Code View and in every file VJE saves." )
				)
			);

			add_field ( indentation, show::CODE_INDENT_SIZE, integer_field ( settings_keys::CODE_INDENT_SIZE, QObject::tr ( "Indent size (spaces)" ), 2, 1, 8, QObject::tr ( "How many spaces make one level of indentation \xE2\x80\x94 in the Code View and in every file VJE saves." ) ) );

			std::vector<SettingsField> codeFormatting;

			add_field ( codeFormatting, show::CODE_SYNTAX_HIGHLIGHTING, yes_no_field ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, QObject::tr ( "Syntax highlighting" ), true, QObject::tr ( "Color keys, strings, numbers and punctuation in the Code View." ) ) );

			add_field
			(
				codeFormatting,
				show::CODE_BRACE_STYLE,
				choice_field
				(
					settings_keys::CODE_BRACE_STYLE,
					QObject::tr ( "Brace style" ),
					{
						{ QObject::tr ( "K&R" ),    settings_values::BRACE_STYLE_K_AND_R },
						{ QObject::tr ( "Allman" ), settings_values::BRACE_STYLE_ALLMAN }
					},
					settings_values::BRACE_STYLE_ALLMAN,
					QObject::tr ( "Put an opening brace at the end of its line (K&R) or on a line of its own (Allman) \xE2\x80\x94 in the Code View and in every file VJE saves." )
				)
			);

			add_field ( codeFormatting, show::CODE_ALIGN_SEPARATORS, yes_no_field ( settings_keys::CODE_ALIGN_SEPARATORS, QObject::tr ( "Align name separators" ), false, QObject::tr ( "Line up the colons after the keys of each object \xE2\x80\x94 in the Code View and in every file VJE saves." ) ) );

			std::vector<SettingsField> behavior;

			add_field
			(
				behavior,
				show::CODE_EDIT_ON,
				choice_field
				(
					settings_keys::CODE_EDIT_ON,
					QObject::tr ( "Edit on" ),
					{
						{ QObject::tr ( "Single click" ), settings_values::EDIT_ON_SINGLE_CLICK },
						{ QObject::tr ( "Double click" ), settings_values::EDIT_ON_DOUBLE_CLICK }
					},
					settings_values::EDIT_ON_DOUBLE_CLICK,
					QObject::tr ( "Whether one click on a node in the tree puts the caret in the Code View, or only selects it until a second click." )
				)
			);

			std::vector<SettingsSection> codeEditor;

			add_section ( codeEditor, QObject::tr ( "Indentation" ), std::move ( indentation ) );
			add_section ( codeEditor, QObject::tr ( "Formatting" ),  std::move ( codeFormatting ) );
			add_section ( codeEditor, QObject::tr ( "Behavior" ),    std::move ( behavior ) );

			add_group ( groups, show::CODE_EDITOR_GROUP, QObject::tr ( "Code Editor" ), std::move ( codeEditor ) );

			//---------------------------------------------------------------------------------------------------------
			// Printing (SET-10, FILE-12). What the page carries besides the content. The rules are the hairlines that
			// separate the file name at the head and the page number at the foot from the body -- on by default,
			// because on a page of preformatted text white space alone reads as a blank line rather than as a margin,
			// and off for anyone printing onto letterhead or scanning the result.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> printSettings;

			add_field
			(
				printSettings,
				show::PRINT_PAGE_RULES,
				yes_no_field ( settings_keys::PRINT_PAGE_RULES, QObject::tr ( "Show page rules" ), true, QObject::tr ( "Draw thin lines separating each printed page's header and footer from its content." ) )
			);

			std::vector<SettingsSection> printing;

			add_section ( printing, QObject::tr ( "Print Settings" ), std::move ( printSettings ) );

			add_group ( groups, show::PRINTING_GROUP, QObject::tr ( "Printing" ), std::move ( printing ) );

			//---------------------------------------------------------------------------------------------------------
			// System (SET-09, SET-15), last. How VJE relates to the operating system, where General holds how it edits
			// a document.
			//
			// Windows Explorer integration comes first, and ONLY WHERE THE PLATFORM OFFERS IT: the platform layer is
			// asked rather than an #ifdef deciding it here, so this is the one place the dialog learns the platform
			// differs, and on Linux the row is simply absent -- absent, not disabled, since there is nothing the user
			// could do to enable it (SET-01b's disabled-not-hidden rule is for a setting that CAN apply).
			//
			// The folder and the file name are inert while logging is off, which the dialog shows by disabling them
			// rather than by hiding them -- the same disabled-not-hidden rule the menus follow.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> windowsExplorer;

			add_field
			(
				windowsExplorer,
				show::EXPLORER_INTEGRATION && platform::explorer_integration::is_supported (),
				yes_no_field ( settings_keys::EXPLORER_INTEGRATION, QObject::tr ( "Windows Explorer integration" ), false, QObject::tr ( "Offer Open with \xE2\x96\xB8 VJE and Edit in VJE in Windows Explorer's menus for .json files." ) )
			);

			std::vector<SettingsField> logging;

			add_field ( logging, show::DIAGNOSTIC_LOGGING, yes_no_field ( settings_keys::DIAGNOSTIC_LOGGING, QObject::tr ( "Enable diagnostic logging" ), false, QObject::tr ( "Write a timestamped record of what VJE does, for tracking down a problem." ) ) );

			SettingsField logFolder;

			logFolder.kind         = SettingsFieldKind::Folder;
			logFolder.key          = settings_keys::LOG_FOLDER;
			logFolder.label        = QObject::tr ( "Log folder" );
			logFolder.description  = QObject::tr ( "Where the log is written. Left empty, VJE uses its own logs folder." );
			logFolder.defaultValue = QString ();
			logFolder.placeholder  = QObject::tr ( "(the application's own logs folder)" );
			logFolder.enabledByKey = settings_keys::DIAGNOSTIC_LOGGING;

			add_field ( logging, show::LOG_FOLDER, logFolder );

			SettingsField logFileName = short_text_field
			(
				settings_keys::LOG_FILE_NAME,
				QObject::tr ( "Log file name" ),
				settings_values::DEFAULT_LOG_FILE_NAME,
				64,
				QObject::tr ( "The file in the log folder that the log is written to." )
			);

			logFileName.enabledByKey = settings_keys::DIAGNOSTIC_LOGGING;

			add_field ( logging, show::LOG_FILE_NAME, logFileName );

			std::vector<SettingsSection> system;

			add_section ( system, QObject::tr ( "Windows Explorer" ), std::move ( windowsExplorer ) );
			add_section ( system, QObject::tr ( "Logging" ),          std::move ( logging ) );

			add_group ( groups, show::SYSTEM_GROUP, QObject::tr ( "System" ), std::move ( system ) );

			//---------------------------------------------------------------------------------------------------------
			// THERE IS NO DEBUG GROUP (2026-08-12). It carried exactly one field -- Icon image format, choosing which
			// of the two committed icon TREES IconLibrary read while both held the same artwork in two formats. SET-13
			// made the format a consequence of the family (Classic is raster, Fluent is vector), so the setting had
			// nothing left to choose and was removed rather than left as a control that does nothing.
			//
			// The group is not kept empty against a future field: add_group already drops an empty one, so an empty
			// vector here would read as a group that exists and is switched off, which is a different statement.
			//---------------------------------------------------------------------------------------------------------

			return groups;
		}
	}

	//=================================================================================================================
	// The schema
	//=================================================================================================================

	const std::vector<SettingsGroup>& settings_schema ()
	{
		// Built once. The order IS SET-02's master-list order, System last; the Toolbar group is filled in by the window
		// (see the header).

		static const std::vector<SettingsGroup> groups = build_schema ();

		return groups;
	}

	bool settings_field_spans_page ( SettingsFieldKind kind )
	{
		return kind == SettingsFieldKind::TransferList;
	}

	SettingsGroup toolbar_group ( const std::vector<ToolbarCommand>& catalogue )
	{
		SettingsGroup group;

		group.title = QObject::tr ( "Toolbar" );

		SettingsField field;

		field.kind  = SettingsFieldKind::TransferList;
		field.key   = settings_keys::TOOLBAR_LAYOUT;
		field.label = QObject::tr ( "Toolbar buttons" );

		field.chosenListLabel    = QObject::tr ( "Toolbar" );
		field.availableListLabel = QObject::tr ( "Available commands" );

		// The separator is the one repeatable entry: the user places as many as they want, and taking one off the bar
		// deletes it rather than returning it to a list it never left (SET-04).

		field.repeatableValue = toolbar_names::SEPARATOR;

		SettingsOption separator;

		separator.value = toolbar_names::SEPARATOR;
		separator.label = QObject::tr ( "——— Separator ———" );

		field.options.append ( separator );

		// Then the catalogue, in catalogue order -- which is menu order, and is what the Available list shows. Each
		// option carries the command's own label and its toolbar glyph, so the two lists read as the toolbar does.

		for ( const ToolbarCommand& command : catalogue )
		{
			if ( command.action == nullptr )
			{
				continue;
			}

			SettingsOption option;

			option.value = command.name;
			option.label = toolbar_command_label ( command.action );
			option.icon  = command.action->icon ();

			field.options.append ( option );
		}

		// The shipped layout, which is what Restore Defaults returns to AND what a first run shows: the snapshot reads
		// the store where a value exists and falls back here where none does, and by the time this dialog can open the
		// window has already resolved a first run (or a migration) through stored_toolbar_layout. So the CURRENT layout
		// needs no separate channel -- it is either in the store or it is this.

		field.defaultValue = default_toolbar_layout ();

		// One UNTITLED section, so the page draws no box (SET-01c): the transfer list is the page, and a box around the
		// whole of it would divide nothing. Its captions are its own, inside the control.

		SettingsSection section;

		section.fields.push_back ( field );

		group.sections.push_back ( section );

		return group;
	}

	std::vector<SettingsGroup> settings_schema_with_toolbar ( const std::vector<ToolbarCommand>& catalogue )
	{
		std::vector<SettingsGroup> groups = settings_schema ();

		const SettingsGroup toolbar = toolbar_group ( catalogue );

		for ( SettingsGroup& group : groups )
		{
			if ( group.title == toolbar.title )
			{
				group.sections = toolbar.sections;
			}
		}

		return groups;
	}

	std::vector<const SettingsField*> settings_fields ( const SettingsGroup& group )
	{
		std::vector<const SettingsField*> fields;

		for ( const SettingsSection& section : group.sections )
		{
			for ( const SettingsField& field : section.fields )
			{
				fields.push_back ( &field );
			}
		}

		return fields;
	}

	std::vector<const SettingsField*> settings_fields ( const std::vector<SettingsGroup>& groups )
	{
		std::vector<const SettingsField*> fields;

		for ( const SettingsGroup& group : groups )
		{
			const std::vector<const SettingsField*> groupFields = settings_fields ( group );

			fields.insert ( fields.end (), groupFields.begin (), groupFields.end () );
		}

		return fields;
	}

	//=================================================================================================================
	// SettingsSnapshot
	//=================================================================================================================

	SettingsSnapshot::SettingsSnapshot ( const std::vector<SettingsGroup>& groups, const SettingsStore* store )
	{
		for ( const SettingsField* const field : settings_fields ( groups ) )
		{
			kinds.insert ( field->key, field->kind );

			switch ( field->kind )
			{
				case SettingsFieldKind::YesNo:
				case SettingsFieldKind::CheckBox:
				{
					const bool defaultValue = field->defaultValue.toBool ();

					values.insert ( field->key, ( store != nullptr ) ? store->value_bool ( field->key, defaultValue ) : defaultValue );

					break;
				}

				case SettingsFieldKind::Integer:
				{
					const int defaultValue = field->defaultValue.toInt ();

					values.insert ( field->key, ( store != nullptr ) ? store->value_int ( field->key, defaultValue ) : defaultValue );

					break;
				}

				case SettingsFieldKind::TransferList:
				{
					// contains() rather than a value-with-fallback, and that is the whole point: an EMPTY stored
					// list is a legal value (SET-04's empty toolbar) and an isEmpty() test would silently replace
					// it with the default every time the dialog opened.

					const QStringList defaultValue = field->defaultValue.toStringList ();
					const bool        stored       = ( store != nullptr ) && store->contains ( field->key );

					values.insert ( field->key, stored ? store->value_string_list ( field->key ) : defaultValue );

					break;
				}

				default:
				{
					const QString defaultValue = field->defaultValue.toString ();

					values.insert ( field->key, ( store != nullptr ) ? store->value_string ( field->key, defaultValue ) : defaultValue );

					break;
				}
			}
		}
	}

	QString SettingsSnapshot::value_string ( const QString& key ) const
	{
		return values.value ( key ).toString ();
	}

	bool SettingsSnapshot::value_bool ( const QString& key ) const
	{
		return values.value ( key ).toBool ();
	}

	int SettingsSnapshot::value_int ( const QString& key ) const
	{
		return values.value ( key ).toInt ();
	}

	QStringList SettingsSnapshot::value_string_list ( const QString& key ) const
	{
		return values.value ( key ).toStringList ();
	}

	bool SettingsSnapshot::is_field_enabled ( const SettingsField& field ) const
	{
		if ( field.enabledByKey.isEmpty () )
		{
			return true;
		}

		// The EDIT state, not the stored state: switching logging on must enable its folder there and then, before OK has
		// written anything (SET-09).

		return value_bool ( field.enabledByKey );
	}

	void SettingsSnapshot::set_string ( const QString& key, const QString& value )
	{
		values.insert ( key, value );
	}

	void SettingsSnapshot::set_bool ( const QString& key, bool value )
	{
		values.insert ( key, value );
	}

	void SettingsSnapshot::set_int ( const QString& key, int value )
	{
		values.insert ( key, value );
	}

	void SettingsSnapshot::set_string_list ( const QString& key, const QStringList& value )
	{
		values.insert ( key, value );
	}

	QStringList SettingsSnapshot::apply ( SettingsStore* store ) const
	{
		QStringList changedKeys;

		if ( store == nullptr )
		{
			return changedKeys;
		}

		// One pass over every field the dialog holds (SET-01 / SET-08). The store's setters dedupe, so a value the user
		// never touched is neither rewritten nor signalled -- which is what stops an untouched Settings visit from
		// re-rendering every view.

		for ( auto entry = values.constBegin (); entry != values.constEnd (); ++entry )
		{
			const QString&          key  = entry.key ();
			const SettingsFieldKind kind = kinds.value ( key, SettingsFieldKind::ShortText );

			bool changed = false;

			switch ( kind )
			{
				case SettingsFieldKind::YesNo:
				case SettingsFieldKind::CheckBox:
				{
					changed = store->set_bool ( key, entry.value ().toBool () );

					break;
				}

				case SettingsFieldKind::Integer:
				{
					changed = store->set_int ( key, entry.value ().toInt () );

					break;
				}

				case SettingsFieldKind::TransferList:
				{
					changed = store->set_string_list ( key, entry.value ().toStringList () );

					break;
				}

				default:
				{
					changed = store->set_string ( key, entry.value ().toString () );

					break;
				}
			}

			if ( changed )
			{
				changedKeys.append ( key );
			}
		}

		return changedKeys;
	}
}

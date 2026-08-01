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
#include "services/SettingsStore.hpp"

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

		SettingsField choice_field ( const QString& key, const QString& label, const QList<SettingsOption>& options, const QString& defaultValue )
		{
			SettingsField field;

			field.kind         = SettingsFieldKind::Choice;
			field.key          = key;
			field.label        = label;
			field.options      = options;
			field.defaultValue = defaultValue;

			return field;
		}

		SettingsField yes_no_field ( const QString& key, const QString& label, bool defaultValue )
		{
			SettingsField field;

			field.kind         = SettingsFieldKind::YesNo;
			field.key          = key;
			field.label        = label;
			field.defaultValue = defaultValue;

			return field;
		}

		SettingsField integer_field ( const QString& key, const QString& label, int defaultValue, int minimum, int maximum )
		{
			SettingsField field;

			field.kind           = SettingsFieldKind::Integer;
			field.key            = key;
			field.label          = label;
			field.defaultValue   = defaultValue;
			field.minimumInteger = minimum;
			field.maximumInteger = maximum;

			return field;
		}

		SettingsField short_text_field ( const QString& key, const QString& label, const QString& defaultValue, int maximumLength )
		{
			SettingsField field;

			field.kind          = SettingsFieldKind::ShortText;
			field.key           = key;
			field.label         = label;
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

		// A group joins the master list only when it is switched on AND has something to show, so the list never offers
		// an empty page. allowEmpty is the Toolbar group's exception: it is deliberately empty here because its fields
		// are the window's own buttons (SET-04), filled in by settings_schema_with_toolbar.

		void add_group
		(
			std::vector<SettingsGroup>& groups,
			bool                        visible,
			const QString&              title,
			std::vector<SettingsField>  fields,
			bool                        allowEmpty = false
		)
		{
			if ( !visible || ( fields.empty () && !allowEmpty ) )
			{
				return;
			}

			SettingsGroup group;

			group.title  = title;
			group.fields = std::move ( fields );

			groups.push_back ( group );
		}

		std::vector<SettingsGroup> build_schema ()
		{
			namespace show = config::settings_dialog::show;

			std::vector<SettingsGroup> groups;

			//---------------------------------------------------------------------------------------------------------
			// General (SET-03). Theme is the SAME setting as View > Theme, mirrored -- the dialog routes it through
			// ThemeService so the palette follows the value (section 2.9).
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> general;

			add_field
			(
				general,
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
					settings_values::THEME_LIGHT
				)
			);

			add_field
			(
				general,
				show::CHECK_UPDATES,
				yes_no_field ( settings_keys::CHECK_UPDATES, QObject::tr ( "Check for updates automatically" ), true )
			);

			add_field
			(
				general,
				show::ON_DUPLICATE_KEYS,
				choice_field
				(
					settings_keys::ON_DUPLICATE_KEYS,
					QObject::tr ( "On duplicate keys when loading" ),
					{
						{ QObject::tr ( "Keep silently" ), settings_values::ON_DUPLICATE_KEEP_SILENTLY },
						{ QObject::tr ( "Keep and warn" ), settings_values::ON_DUPLICATE_KEEP_AND_WARN }
					},
					settings_values::ON_DUPLICATE_KEEP_SILENTLY
				)
			);

			// SET-03's String display. One setting for the Form View and the Text View, which is why it sits in General
			// rather than in either view's group.

			add_field
			(
				general,
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
					settings_values::STRING_DISPLAY_ESCAPED
				)
			);

			// SET-03's pane corners (STYLE-02). In General rather than in either pane's group because it governs BOTH
			// cards, for the same reason String display sits here rather than in the Form or Text View group.
			//
			// The default is named rather than written out: config::card::ROUNDED_TOP_CORNERS_DEFAULT is the one
			// statement of it, shared with the reader in settings_profiles.

			add_field
			(
				general,
				show::ROUNDED_PANE_CORNERS,
				yes_no_field
				(
					settings_keys::ROUNDED_PANE_CORNERS,
					QObject::tr ( "Rounded pane corners" ),
					config::card::ROUNDED_TOP_CORNERS_DEFAULT
				)
			);

			add_group ( groups, show::GENERAL_GROUP, QObject::tr ( "General" ), std::move ( general ) );

			//---------------------------------------------------------------------------------------------------------
			// Toolbar (SET-04). Present here so the master list keeps SET-02's order; its fields come from the toolbar.
			//---------------------------------------------------------------------------------------------------------

			add_group ( groups, show::TOOLBAR_GROUP, QObject::tr ( "Toolbar" ), {}, true );

			//---------------------------------------------------------------------------------------------------------
			// Form View Editor (SET-05).
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> formView;

			add_field
			(
				formView,
				show::FORM_EDIT_ON,
				choice_field
				(
					settings_keys::FORM_EDIT_ON,
					QObject::tr ( "Edit on" ),
					{
						{ QObject::tr ( "Single click" ), settings_values::EDIT_ON_SINGLE_CLICK },
						{ QObject::tr ( "Double click" ), settings_values::EDIT_ON_DOUBLE_CLICK }
					},
					settings_values::EDIT_ON_DOUBLE_CLICK
				)
			);

			add_field
			(
				formView,
				show::FORM_ALLOW_JAGGED_PASTE,
				yes_no_field ( settings_keys::FORM_ALLOW_JAGGED_PASTE, QObject::tr ( "Allow jagged-array paste" ), false )
			);

			// EDIT-02. Default Yes: the editor is fully editable unless the user asks otherwise. Switching it off reaches
			// BOTH routes to a rename -- the key column and the Rename Key command -- through key_editing_allowed().

			add_field
			(
				formView,
				show::FORM_ALLOW_KEY_EDITING,
				yes_no_field ( settings_keys::FORM_ALLOW_KEY_EDITING, QObject::tr ( "Allow key editing" ), true )
			);

			add_field
			(
				formView,
				show::FORM_WRAP_STRINGS,
				yes_no_field ( settings_keys::FORM_WRAP_STRINGS, QObject::tr ( "Wrap strings" ), false )
			);

			add_group ( groups, show::FORM_VIEW_GROUP, QObject::tr ( "Form View Editor" ), std::move ( formView ) );

			//---------------------------------------------------------------------------------------------------------
			// Text View (SET-06). The rendering options of EDITOR-06, in the spec's table order.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> textView;

			add_field ( textView, show::TEXT_WRAP_STRINGS,     yes_no_field     ( settings_keys::TEXT_WRAP_STRINGS,     QObject::tr ( "Wrap strings" ), false ) );
			add_field ( textView, show::TEXT_BLANK_LINES,      integer_field    ( settings_keys::TEXT_BLANK_LINES,      QObject::tr ( "Blank lines between fields" ), 0, 0, 10 ) );
			add_field ( textView, show::TEXT_ALIGN_SEPARATORS, yes_no_field     ( settings_keys::TEXT_ALIGN_SEPARATORS, QObject::tr ( "Align name separators" ), true ) );
			add_field ( textView, show::TEXT_NAME_SEPARATOR,   short_text_field ( settings_keys::TEXT_NAME_SEPARATOR,   QObject::tr ( "Name separator" ), QStringLiteral ( ":" ), 3 ) );
			add_field ( textView, show::TEXT_INCLUDE_OBJECTS,  yes_no_field     ( settings_keys::TEXT_INCLUDE_OBJECTS,  QObject::tr ( "Include object names" ), true ) );
			add_field ( textView, show::TEXT_INCLUDE_ARRAYS,   yes_no_field     ( settings_keys::TEXT_INCLUDE_ARRAYS,   QObject::tr ( "Include array names" ), true ) );

			add_field
			(
				textView,
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
					settings_values::MARKDOWN_STYLE_NONE
				)
			);

			add_field
			(
				textView,
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
					settings_values::TABLE_STYLE_COLUMNAR
				)
			);

			add_group ( groups, show::TEXT_VIEW_GROUP, QObject::tr ( "Text View" ), std::move ( textView ) );

			//---------------------------------------------------------------------------------------------------------
			// Code Editor (SET-07). The first four ARE the document format profile, shared verbatim with File > Save
			// (FILE-03) -- so editing them here changes what a save writes, and re-formats the Code View's text the
			// moment OK commits. That is the contract, not a side effect.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> codeEditor;

			add_field
			(
				codeEditor,
				show::CODE_INDENT_KIND,
				choice_field
				(
					settings_keys::CODE_INDENT_KIND,
					QObject::tr ( "Indentation" ),
					{
						{ QObject::tr ( "Spaces" ), settings_values::INDENT_SPACES },
						{ QObject::tr ( "Tabs" ),   settings_values::INDENT_TABS }
					},
					settings_values::INDENT_SPACES
				)
			);

			add_field ( codeEditor, show::CODE_INDENT_SIZE,         integer_field ( settings_keys::CODE_INDENT_SIZE, QObject::tr ( "Indent size (spaces)" ), 2, 1, 8 ) );
			add_field ( codeEditor, show::CODE_SYNTAX_HIGHLIGHTING, yes_no_field  ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, QObject::tr ( "Syntax highlighting" ), true ) );

			add_field
			(
				codeEditor,
				show::CODE_BRACE_STYLE,
				choice_field
				(
					settings_keys::CODE_BRACE_STYLE,
					QObject::tr ( "Brace style" ),
					{
						{ QObject::tr ( "K&R" ),    settings_values::BRACE_STYLE_K_AND_R },
						{ QObject::tr ( "Allman" ), settings_values::BRACE_STYLE_ALLMAN }
					},
					settings_values::BRACE_STYLE_ALLMAN
				)
			);

			add_field ( codeEditor, show::CODE_ALIGN_SEPARATORS, yes_no_field ( settings_keys::CODE_ALIGN_SEPARATORS, QObject::tr ( "Align name separators" ), false ) );

			add_field
			(
				codeEditor,
				show::CODE_EDIT_ON,
				choice_field
				(
					settings_keys::CODE_EDIT_ON,
					QObject::tr ( "Edit on" ),
					{
						{ QObject::tr ( "Single click" ), settings_values::EDIT_ON_SINGLE_CLICK },
						{ QObject::tr ( "Double click" ), settings_values::EDIT_ON_DOUBLE_CLICK }
					},
					settings_values::EDIT_ON_DOUBLE_CLICK
				)
			);

			add_group ( groups, show::CODE_EDITOR_GROUP, QObject::tr ( "Code Editor" ), std::move ( codeEditor ) );

			//---------------------------------------------------------------------------------------------------------
			// Printing (SET-10, FILE-12). What the page carries besides the content. The rules are the hairlines that
			// separate the file name at the head and the page number at the foot from the body -- on by default,
			// because on a page of preformatted text white space alone reads as a blank line rather than as a margin,
			// and off for anyone printing onto letterhead or scanning the result.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> printing;

			add_field
			(
				printing,
				show::PRINT_PAGE_RULES,
				yes_no_field ( settings_keys::PRINT_PAGE_RULES, QObject::tr ( "Show page rules" ), true )
			);

			add_group ( groups, show::PRINTING_GROUP, QObject::tr ( "Printing" ), std::move ( printing ) );

			//---------------------------------------------------------------------------------------------------------
			// System (SET-09), last. The folder and the file name are inert while logging is off, which the dialog shows
			// by disabling them rather than by hiding them -- the same disabled-not-hidden rule the menus follow.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> system;

			add_field ( system, show::DIAGNOSTIC_LOGGING, yes_no_field ( settings_keys::DIAGNOSTIC_LOGGING, QObject::tr ( "Enable diagnostic logging" ), false ) );

			SettingsField logFolder;

			logFolder.kind         = SettingsFieldKind::Folder;
			logFolder.key          = settings_keys::LOG_FOLDER;
			logFolder.label        = QObject::tr ( "Log folder" );
			logFolder.defaultValue = QString ();
			logFolder.placeholder  = QObject::tr ( "(the application's own logs folder)" );
			logFolder.enabledByKey = settings_keys::DIAGNOSTIC_LOGGING;

			add_field ( system, show::LOG_FOLDER, logFolder );

			SettingsField logFileName = short_text_field
			(
				settings_keys::LOG_FILE_NAME,
				QObject::tr ( "Log file name" ),
				settings_values::DEFAULT_LOG_FILE_NAME,
				64
			);

			logFileName.enabledByKey = settings_keys::DIAGNOSTIC_LOGGING;

			add_field ( system, show::LOG_FILE_NAME, logFileName );

			add_group ( groups, show::SYSTEM_GROUP, QObject::tr ( "System" ), std::move ( system ) );

			//---------------------------------------------------------------------------------------------------------
			// Debug, after System because it is not one of SET-02's user-facing groups at all -- developer tooling,
			// switched off by show::DEBUG_GROUP for a release build.
			//
			// ONE SETTING, and it earns its place through the icon-authoring loop rather than through the antialiasing
			// experiment it was originally built for. Under ArtworkSource::Raster the PNG tree
			// is what a human drew and the SVG set is traced from it, so selecting Png shows the author the exact file
			// they edited rather than a transcription of it -- which is the check that a newly drawn master looks right
			// at the size it will actually be used.
			//---------------------------------------------------------------------------------------------------------

			std::vector<SettingsField> debugFields;

			add_field
			(
				debugFields,
				show::DEBUG_ICON_SOURCE,
				choice_field
				(
					settings_keys::DEBUG_ICON_SOURCE,
					QObject::tr ( "Icon image format" ),
					{
						{ QObject::tr ( "SVG" ), settings_values::ICON_SOURCE_SVG },
						{ QObject::tr ( "PNG" ), settings_values::ICON_SOURCE_PNG }
					},
					( config::icons::DEFAULT_ICON_SOURCE == config::icons::IconSource::Svg )
						? settings_values::ICON_SOURCE_SVG
						: settings_values::ICON_SOURCE_PNG
				)
			);

			add_group ( groups, show::DEBUG_GROUP, QObject::tr ( "Debug" ), std::move ( debugFields ) );

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

		group.fields.push_back ( field );

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
				group.fields = toolbar.fields;
			}
		}

		return groups;
	}

	//=================================================================================================================
	// SettingsSnapshot
	//=================================================================================================================

	SettingsSnapshot::SettingsSnapshot ( const std::vector<SettingsGroup>& groups, const SettingsStore* store )
	{
		for ( const SettingsGroup& group : groups )
		{
			for ( const SettingsField& field : group.fields )
			{
				kinds.insert ( field.key, field.kind );

				switch ( field.kind )
				{
					case SettingsFieldKind::YesNo:
					case SettingsFieldKind::CheckBox:
					{
						const bool defaultValue = field.defaultValue.toBool ();

						values.insert ( field.key, ( store != nullptr ) ? store->value_bool ( field.key, defaultValue ) : defaultValue );

						break;
					}

					case SettingsFieldKind::Integer:
					{
						const int defaultValue = field.defaultValue.toInt ();

						values.insert ( field.key, ( store != nullptr ) ? store->value_int ( field.key, defaultValue ) : defaultValue );

						break;
					}

					case SettingsFieldKind::TransferList:
					{
						// contains() rather than a value-with-fallback, and that is the whole point: an EMPTY stored
						// list is a legal value (SET-04's empty toolbar) and an isEmpty() test would silently replace
						// it with the default every time the dialog opened.

						const QStringList defaultValue = field.defaultValue.toStringList ();
						const bool        stored       = ( store != nullptr ) && store->contains ( field.key );

						values.insert ( field.key, stored ? store->value_string_list ( field.key ) : defaultValue );

						break;
					}

					default:
					{
						const QString defaultValue = field.defaultValue.toString ();

						values.insert ( field.key, ( store != nullptr ) ? store->value_string ( field.key, defaultValue ) : defaultValue );

						break;
					}
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

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   toolbar_catalogue implementation. See the header for why the setting is an ordered list, and why the catalogue and
//   the default layout are stated separately.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/toolbar_catalogue.hpp"

#include "services/SettingsStore.hpp"

#include <QAction>
#include <QSet>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// The superseded per-button visibility key (SET-04 before 2026-07-27). Kept only so stored_toolbar_layout can
		// read a pre-existing settings file once and then delete it; nothing else in the application knows it exists.
		//-------------------------------------------------------------------------------------------------------------

		QString legacy_visibility_key ( const QString& name )
		{
			return settings_keys::TOOLBAR_VISIBLE_PREFIX + name;
		}

		bool catalogue_contains ( const std::vector<ToolbarCommand>& catalogue, const QString& name )
		{
			for ( const ToolbarCommand& command : catalogue )
			{
				if ( command.name == name )
				{
					return true;
				}
			}

			return false;
		}
	}

	//=================================================================================================================
	// The default layout
	//=================================================================================================================

	QStringList default_toolbar_layout ()
	{
		namespace names = toolbar_names;

		// Section 2.4's four groups. This is a CURATED subset of the catalogue, not the whole of it -- the commands
		// absent here are exactly what the Settings dialog's Available list offers on a first run.

		return
		{
			names::NEW, names::OPEN, names::CLOSE, names::SAVE, names::SAVE_AS,
			names::SEPARATOR,
			names::UNDO, names::REDO,
			names::SEPARATOR,
			names::ADD_OBJECT, names::ADD_ARRAY, names::ADD_STRING, names::ADD_NUMBER, names::ADD_BOOLEAN, names::ADD_NULL,
			names::SEPARATOR,
			names::MOVE_UP, names::MOVE_DOWN
		};
	}

	//=================================================================================================================
	// Normalization
	//=================================================================================================================

	QStringList normalized_toolbar_layout ( const QStringList& layout, const std::vector<ToolbarCommand>& catalogue )
	{
		QStringList   normalized;
		QSet<QString> placed;

		for ( const QString& entry : layout )
		{
			// The one repeatable entry: never de-duplicated, never dropped, never moved (see the header).

			if ( entry == toolbar_names::SEPARATOR )
			{
				normalized.append ( entry );

				continue;
			}

			if ( placed.contains ( entry ) || !catalogue_contains ( catalogue, entry ) )
			{
				continue;
			}

			normalized.append ( entry );
			placed.insert ( entry );
		}

		return normalized;
	}

	//=================================================================================================================
	// Reading the stored layout, and the one-time migration
	//=================================================================================================================

	QStringList stored_toolbar_layout ( SettingsStore* settings, const std::vector<ToolbarCommand>& catalogue )
	{
		if ( settings == nullptr )
		{
			return normalized_toolbar_layout ( default_toolbar_layout (), catalogue );
		}

		// The current key wins outright. contains() rather than isEmpty(): a stored EMPTY list is a legal layout
		// meaning "no buttons at all", and must not be mistaken for a first run.

		if ( settings->contains ( settings_keys::TOOLBAR_LAYOUT ) )
		{
			return normalized_toolbar_layout ( settings->value_string_list ( settings_keys::TOOLBAR_LAYOUT ), catalogue );
		}

		// No current key. Either a first run, or a settings file written before 2026-07-27 that carries one visibility
		// key per button. Collect whichever of those are actually present -- only the fifteen buttons that shipped on
		// the bar ever had one, and only after the user opened the Settings dialog at least once.

		QStringList legacyKeysFound;
		QSet<QString> hiddenByLegacyKey;

		for ( const ToolbarCommand& command : catalogue )
		{
			const QString key = legacy_visibility_key ( command.name );

			if ( !settings->contains ( key ) )
			{
				continue;
			}

			legacyKeysFound.append ( key );

			if ( !settings->value_bool ( key, true ) )
			{
				hiddenByLegacyKey.insert ( command.name );
			}
		}

		QStringList layout = normalized_toolbar_layout ( default_toolbar_layout (), catalogue );

		if ( legacyKeysFound.isEmpty () )
		{
			// A genuine first run. Nothing is written: the default layout applies until the user changes something,
			// so an untouched installation carries no toolbar key at all.

			return layout;
		}

		// Migrate. The old model had a fixed order, so the migrated layout is the default order minus the unchecked
		// buttons; any separator the removal strands is left in place and collapsed at render, exactly as before.

		QStringList migrated;

		for ( const QString& entry : layout )
		{
			if ( !hiddenByLegacyKey.contains ( entry ) )
			{
				migrated.append ( entry );
			}
		}

		settings->set_string_list ( settings_keys::TOOLBAR_LAYOUT, migrated );

		for ( const QString& key : legacyKeysFound )
		{
			settings->remove ( key );
		}

		return migrated;
	}

	//=================================================================================================================
	// Labels
	//=================================================================================================================

	QString toolbar_command_label ( const QAction* action )
	{
		if ( action == nullptr )
		{
			return QString ();
		}

		QString label = action->text ();

		label.remove ( QLatin1Char ( '&' ) );
		label.remove ( QStringLiteral ( "..." ) );

		return label.trimmed ();
	}
}

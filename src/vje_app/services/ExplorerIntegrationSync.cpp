//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ExplorerIntegrationSync implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/ExplorerIntegrationSync.hpp"

#include "services/settings_profiles.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QDir>
#include <QFileInfo>

namespace vje
{
	namespace explorer = platform::explorer_integration;

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	ExplorerIntegrationSync::ExplorerIntegrationSync
	(
		SettingsStore* settings,
		const QString& windowProgramPath,
		const QString& classesRoot,
		QObject*       parent
	)
		: QObject       ( parent )
		, settings      ( settings )
		, windowProgram ( windowProgramPath )
		, classesRoot   ( classesRoot )
	{
		connect ( settings, &SettingsStore::changed, this, &ExplorerIntegrationSync::setting_changed );
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	void ExplorerIntegrationSync::refresh ()
	{
		if ( !explorer::is_supported () || !explorer_integration_enabled ( settings ) )
		{
			return;
		}

		// Rewritten only when something differs, so an ordinary launch reads eight values and writes none -- and does
		// not make Explorer re-read every association on every start.

		if ( !explorer::is_registered_for ( windowProgram, classesRoot ) )
		{
			apply ( true );
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void ExplorerIntegrationSync::setting_changed ( const QString& key )
	{
		if ( ( key != settings_keys::EXPLORER_INTEGRATION ) || correcting || !explorer::is_supported () )
		{
			return;
		}

		apply ( explorer_integration_enabled ( settings ) );
	}

	void ExplorerIntegrationSync::apply ( bool enabled )
	{
		explorer::Outcome outcome;

		if ( !enabled )
		{
			outcome = explorer::unregister ( classesRoot );
		}
		else if ( !QFileInfo ( windowProgram ).isFile () )
		{
			outcome = { false, tr ( "%1 does not exist." ).arg ( QDir::toNativeSeparators ( windowProgram ) ) };
		}
		else
		{
			outcome = explorer::register_for ( windowProgram, classesRoot );
		}

		if ( outcome.succeeded )
		{
			return;
		}

		// The setting records what the registry now holds: after a failed registration (rolled back), nothing for this
		// program; after a failed removal, whatever could not be removed.

		const bool registered = enabled ? explorer::is_registered_for ( windowProgram, classesRoot )
		                                : explorer::is_registered ( classesRoot );

		correcting = true;

		settings->set_bool ( settings_keys::EXPLORER_INTEGRATION, registered );

		correcting = false;

		emit failed ( outcome.problem );
	}
}

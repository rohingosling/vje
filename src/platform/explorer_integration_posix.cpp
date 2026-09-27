//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   explorer_integration, POSIX -- the feature is not offered (FILE-15, SET-15). Integrating with Linux file managers
//   is explored at a later stage; until then every question is answered "no" and every operation reports that it is
//   unsupported. Nothing reaches these in a shipped build: the settings schema and the command registry ask
//   is_supported () first, so there is no row and no command that could call them.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/explorer_integration.hpp>

namespace vje::platform::explorer_integration
{
	namespace
	{
		Outcome unsupported ()
		{
			return { false, QStringLiteral ( "Windows Explorer integration is not available on this platform." ) };
		}
	}

	bool is_supported ()
	{
		return false;
	}

	Outcome register_for ( const QString& windowProgramPath, const QString& classesRoot )
	{
		Q_UNUSED ( windowProgramPath )
		Q_UNUSED ( classesRoot )

		return unsupported ();
	}

	Outcome unregister ( const QString& classesRoot )
	{
		Q_UNUSED ( classesRoot )

		return unsupported ();
	}

	bool is_registered ( const QString& classesRoot )
	{
		Q_UNUSED ( classesRoot )

		return false;
	}

	bool is_registered_for ( const QString& windowProgramPath, const QString& classesRoot )
	{
		Q_UNUSED ( windowProgramPath )
		Q_UNUSED ( classesRoot )

		return false;
	}
}

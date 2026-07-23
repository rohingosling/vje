//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Implementation of the application name / version API. VJE_APPLICATION_NAME and VJE_VERSION_STRING are supplied
//   as compile definitions by src/vje_core/CMakeLists.txt, which sources the version from the top-level project().
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/version.hpp>

namespace vje
{
	QString application_name ()
	{
		return QStringLiteral ( VJE_APPLICATION_NAME );
	}

	QString version_string ()
	{
		return QStringLiteral ( VJE_VERSION_STRING );
	}

	QString version_line ()
	{
		return application_name () + QLatin1Char ( ' ' ) + version_string ();
	}
}

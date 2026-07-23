//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Single source of truth for the application's name and version. The values originate from the CMake project
//   version (passed in as compile definitions) so that vje_app's --version output, the window title, and the
//   About box can never drift from the build's actual version.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	// Human-readable application name ("VJE").

	QString application_name ();

	// Semantic version string (e.g. "2.0.0"), sourced from the CMake project version.

	QString version_string ();

	// One-line "<name> <version>" descriptor, used for --version output and the window title.

	QString version_line ();
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   application_identity -- the name, organisation and version every application object VJE constructs carries.
//
//   STATED ONCE because there are now three places an application object is made: the window's QApplication, a
//   command's QCoreApplication, and the console twin's. QStandardPaths derives the settings location from these
//   values, so a command that touches settings (the Explorer pair, CLI-08) reads the file the window writes only if
//   the three agree -- and the way to make three places agree is to have one.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

namespace vje::cli
{
	// Stamp the running application object. Call once, straight after constructing it.

	void apply_application_identity ();
}

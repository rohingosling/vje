//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   window_program -- where the executable that opens VJE's window is, asked from whichever executable is running.
//
//   THREE CALLERS NEED THE SAME ANSWER. The Windows console twin hands a window launch to it (CLI-07); the Explorer
//   pair registers it, since Explorer must launch the window program and never the twin (CLI-08); and the window
//   program itself, at launch, checks that the Explorer entries still name it (FILE-15). Each asks this one function,
//   so "the window program" cannot mean three slightly different files.
//
//   THE ANSWER IS FOUND BY THIS EXECUTABLE'S OWN NAME. On Windows it is the file beside the running executable with
//   the same base name and ".exe" -- so vje.com finds vje.exe, vje.exe finds itself, and a pair renamed together
//   still finds its partner (tst_cli_process runs a renamed copy of the twin beside a stand-in). On Linux the one
//   executable is the window program.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje::cli
{
	// The absolute path of the window program, in Qt's form (forward slashes). Needs an application object, since it
	// starts from QCoreApplication::applicationFilePath (). The file is not checked for; callers that act on it do.

	QString window_program_path ();
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   process_arguments -- the command line as Unicode, before any application object exists (CLI-02).
//
//   THE DEFECT THIS REPLACES. main.cpp read every argument with QString::fromLocal8Bit ( argv [ i ] ). On Windows argv
//   carries the ANSI code page, so a file name with characters outside it had already been turned into question marks
//   before VJE saw it, and the file failed to open -- measured with "données-ñ-日本.json" on a Windows 11 machine whose
//   ANSI code page is 1252: the CJK characters arrived as "??" (V1). Windows keeps the real command line as UTF-16, and
//   that is what this reads.
//
//   WHY NOT QCoreApplication::arguments (). It does read the wide command line on Windows -- but only while argc and
//   argv are exactly as main received them. QApplication removes its own options ("-platform", "-style") from argv as
//   it starts, and from then on Qt falls back to argv, and so to the ANSI code page, which is the defect again for
//   anyone who launches with a Qt option. And the answer is needed BEFORE any application object exists anyway: which
//   object to construct -- QCoreApplication for a command, QApplication for the window -- is the first thing the
//   command line decides (architecture.md section 13.2).
//
//   On POSIX, argv IS the command line, in the locale's encoding, which fromLocal8Bit reads correctly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QStringList>

namespace vje::platform
{
	// Every argument, the program name first, exactly as the user typed them. Takes argc and argv only as the
	// fallback for a command line the wide route cannot account for: if Windows' own split does not produce argc
	// arguments, the two parsers disagree about the line, and argv -- what the C runtime handed main -- wins.

	QStringList command_line_arguments ( int argc, char* argv [] );
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   command_definitions -- the one statement of the command line, and the two things built from it: the parser and
//   the help (CLI-01, architecture.md section 13.2).
//
//   THE STATEMENT IS SPREAD OVER THE COMMANDS AND GATHERED HERE. Each ICliCommand states its own options, operands and
//   summary; what belongs to no command -- the window launch's one positional file, the toolkit's own options, the
//   exit codes -- is stated in this file. configure_parser and help_text then walk the SAME registry, which is what
//   makes "accepted" and "documented" one set.
//
//   THE HELP IS RENDERED HERE RATHER THAN BY QCommandLineParser::helpText (). helpText names the executable FILE --
//   "vje.com", "vje.exe", or a test binary's name -- and lays out one flat list of options, with no room for a
//   synopsis per command. What spec section 2.13 promises is the synopsis. Rendering it from the registry keeps
//   CLI-01's one statement while giving the reader the form they can copy from.
//
//   THE TOOLKIT'S OWN OPTIONS ARE PART OF THE DEFINITION, HIDDEN. Qt reads "-platform offscreen", "-style fusion" and a
//   handful of others for a window launch (CLI-02), and they are not VJE's to document. But they must be DESCRIBED to
//   the parser, or "-platform offscreen" reads as an unknown option followed by a file called "offscreen" -- exactly the
//   defect the hand-written loop had. So they are stated here as options with values, hidden from the help, and
//   refused beside a command, where there is no window for them to configure.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QList>
#include <QString>

namespace vje::cli
{
	class CommandRegistry;

	// The name a user types. "vje" on every platform, whichever of the Windows pair is running (CLI-07).

	inline constexpr auto PROGRAM_NAME = "vje";

	// The file operand that means standard input (CLI-06), and the name it is reported under.

	inline constexpr auto STANDARD_INPUT_ARGUMENT = "-";
	inline constexpr auto STANDARD_INPUT_NAME     = "<stdin>";

	// Qt's own window options, as documented for QGuiApplication and QApplication: accepted for a window launch and
	// left for the toolkit to act on, never listed in the help, and a usage error beside a command.

	QList<QCommandLineOption> toolkit_window_options ();

	// Give `parser` every option in the definition: each registered command's trigger and modifiers, then the
	// toolkit's. Single-dash words parse as long options, so "-platform" is one option rather than six short ones.

	void configure_parser ( QCommandLineParser& parser, const CommandRegistry& registry );

	// An option as the help writes it: "-h | --help", "--query <expression>".

	QString option_synopsis ( const QCommandLineOption& option );

	// The --help text, rendered from the registry.

	QString help_text ( const CommandRegistry& registry );

	// A usage error as it is printed: the program name, the problem, and where to look next.

	QString usage_error_text ( const QString& problem );
}

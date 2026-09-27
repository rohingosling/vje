//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ConsoleCliContext -- the context a command runs against in a real process: the process's own streams, through
//   the platform's one writer (platform/console_output, CLI-09).
//
//   TEXT WITH NOWHERE TO GO IS KEPT, NOT DROPPED. vje.exe is a window program, and launched from Explorer or a shortcut
//   it has no standard handles to write through. What a command wrote that the platform could not deliver is collected
//   in undelivered_text, and the window program decides what to do with it -- help, the version and a usage error are
//   shown in a message box because they ARE the answer, and anything else is discarded (CLI-07). The context does not
//   make that decision itself because it cannot: whether text is the answer is a property of the COMMAND, and a
//   context knows nothing about which command is running.
//
//   Both entry points use this class -- vje_app's main and, on Windows, the console twin's -- so the two cannot differ
//   in how they reach a terminal.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/CliContext.hpp>

#include <platform/console_output.hpp>

namespace vje::cli
{
	class ConsoleCliContext : public CliContext
	{
	public:

		void       write_output        ( const QString& text ) override;
		void       write_error         ( const QString& text ) override;
		QByteArray read_standard_input ()                      override;

		// Everything the platform could not deliver, in the order it was written. Empty wherever both streams reach a
		// console, a pipe or a file -- and so, in practice, always empty on Linux.

		const QString& undelivered_text () const;

	private:

		void deliver ( platform::StandardStream stream, const QString& text );

		QString undelivered;
	};
}

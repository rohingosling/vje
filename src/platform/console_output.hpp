//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   console_output -- the one route by which the command line reaches a terminal, a pipe or a file (CLI-09).
//
//   WHY NOT printf OR QTextStream. A Windows console does not read bytes as UTF-8 unless its code page has been set to
//   65001, which nobody can rely on: printed through the C runtime, a file name with characters outside the console's
//   code page arrives as question marks. The console's own wide-character call, WriteConsoleW, takes UTF-16 and shows
//   it whatever the code page is. But WriteConsoleW works ONLY on a console -- handed a pipe or a file it fails -- and
//   a script reading VJE's output wants bytes it can decode. So there are two destinations and two encodings, and the
//   choice between them is made per write, from what the handle actually is:
//
//     a Windows console    UTF-16, through WriteConsoleW
//     a pipe or a file     UTF-8 bytes, through WriteFile (Windows) or fwrite (POSIX)
//
//   On Linux every destination takes UTF-8 bytes, so that half is one call.
//
//   A WRITE THAT REACHES NOTHING IS A NORMAL OUTCOME, not an error, and it is reported rather than swallowed. vje.exe
//   is linked as a window program, and a window program launched from Explorer or a shortcut has no standard handles
//   at all -- or, launched from some terminals, handles it cannot write through. write_text says whether the text was
//   delivered, and the caller keeps what was not: that is what lets vje.exe show help, the version and a usage error
//   in a message box instead (CLI-07). Asking "does this handle look usable?" BEFORE writing was the first design, and
//   it was dropped because the question cannot be answered reliably from outside a real terminal session -- an
//   inherited console handle can look like a destination and still refuse the write. The write itself is the one
//   answer that cannot be wrong.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QByteArray>
#include <QString>

namespace vje::platform
{
	enum class StandardStream
	{
		Output,                                            // Standard output: reports and results.
		Error                                              // Standard error: usage errors and diagnostics.
	};

	// Write `text` to `stream`: UTF-16 to a Windows console, UTF-8 bytes to anything else. Text is written exactly as
	// given -- line endings included, since a bare LF is right for a pipe on both platforms and a console treats it as
	// a line break. Returns whether ALL of it was delivered: false when the stream has no handle, or the handle refused
	// the write. Empty text is trivially delivered.

	bool write_text ( StandardStream stream, const QString& text );

	// Everything on standard input, to end of file, as bytes (CLI-06). The caller decodes them, through the same rule
	// File > Open uses (DocumentIo::decode_text). Empty when there is no standard input at all.

	QByteArray read_standard_input ();
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   console_output, Windows -- see the header for the design.
//
//   THE HANDLE IS ASKED WHAT IT IS ON EVERY WRITE, rather than once at start-up. It costs one GetConsoleMode call, and
//   it keeps the function free of state: nothing to initialize, and nothing that could have been initialized for a
//   handle the process has since replaced.
//
//   GetConsoleMode IS THE TEST FOR A CONSOLE because it is the one call that succeeds on a console handle this process
//   can write through and fails on everything else. GetFileType answers FILE_TYPE_CHAR for a console, but also for NUL
//   and for a serial port, and WriteConsoleW fails on both of those. A handle that is neither gets WriteFile, and if
//   that fails too the text is reported undelivered -- which is the case the window program's message box is for.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/console_output.hpp>

#include <windows.h>

#include <algorithm>

namespace vje::platform
{
	namespace
	{
		// The largest single WriteConsoleW call. The console host has historically refused very large writes outright
		// (the limit was 64 KB of buffer on older Windows), so a long report is written in pieces rather than risking
		// a write that silently shows nothing.

		constexpr DWORD MAXIMUM_CONSOLE_WRITE_CHARACTERS = 8192;

		// How much standard input is read per call.

		constexpr DWORD STANDARD_INPUT_CHUNK_BYTES = 65536;

		// The console's end-of-input character (Ctrl+Z). ReadConsoleW returns it as ordinary text; it is the user
		// saying "that is all".

		constexpr wchar_t CONSOLE_END_OF_INPUT = 0x1A;

		HANDLE handle_for ( StandardStream stream )
		{
			return GetStdHandle ( ( stream == StandardStream::Output ) ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
		}

		bool is_usable ( HANDLE handle )
		{
			return ( handle != nullptr ) && ( handle != INVALID_HANDLE_VALUE );
		}

		bool is_console ( HANDLE handle )
		{
			DWORD mode = 0;

			return GetConsoleMode ( handle, &mode ) != 0;
		}

		bool write_to_console ( HANDLE handle, const QString& text )
		{
			const wchar_t* remainingText       = reinterpret_cast<const wchar_t*> ( text.utf16 () );
			DWORD          remainingCharacters = static_cast<DWORD> ( text.size () );

			while ( remainingCharacters > 0 )
			{
				const DWORD requested = std::min ( remainingCharacters, MAXIMUM_CONSOLE_WRITE_CHARACTERS );
				DWORD       written   = 0;

				if ( !WriteConsoleW ( handle, remainingText, requested, &written, nullptr ) || ( written == 0 ) )
				{
					return false;
				}

				remainingText       += written;
				remainingCharacters -= written;
			}

			return true;
		}

		bool write_bytes ( HANDLE handle, const QByteArray& bytes )
		{
			const char* remainingBytes = bytes.constData ();
			qsizetype   remainingCount = bytes.size ();

			while ( remainingCount > 0 )
			{
				DWORD written = 0;

				if ( !WriteFile ( handle, remainingBytes, static_cast<DWORD> ( remainingCount ), &written, nullptr ) || ( written == 0 ) )
				{
					return false;
				}

				remainingBytes += written;
				remainingCount -= written;
			}

			return true;
		}

		QByteArray read_console ( HANDLE handle )
		{
			// Typed input, read as UTF-16 for the reason output is written that way, then handed back as UTF-8 so the
			// caller has one encoding to decode whichever route the bytes took.

			QString text;
			wchar_t buffer [ 4096 ];

			for ( ;; )
			{
				DWORD read = 0;

				if ( !ReadConsoleW ( handle, buffer, static_cast<DWORD> ( std::size ( buffer ) ), &read, nullptr ) || ( read == 0 ) )
				{
					break;
				}

				const QString chunk = QString::fromWCharArray ( buffer, static_cast<qsizetype> ( read ) );
				const int     end   = chunk.indexOf ( QChar ( CONSOLE_END_OF_INPUT ) );

				if ( end >= 0 )
				{
					text += chunk.left ( end );
					break;
				}

				text += chunk;
			}

			return text.toUtf8 ();
		}

		QByteArray read_stream ( HANDLE handle )
		{
			// A pipe or a file. ReadFile reports end of input as success with zero bytes on a file, and as
			// ERROR_BROKEN_PIPE on a pipe whose writer has closed -- both mean "that is all".

			QByteArray bytes;
			QByteArray chunk ( static_cast<qsizetype> ( STANDARD_INPUT_CHUNK_BYTES ), Qt::Uninitialized );

			for ( ;; )
			{
				DWORD read = 0;

				if ( !ReadFile ( handle, chunk.data (), STANDARD_INPUT_CHUNK_BYTES, &read, nullptr ) || ( read == 0 ) )
				{
					break;
				}

				bytes.append ( chunk.constData (), static_cast<qsizetype> ( read ) );
			}

			return bytes;
		}
	}

	bool write_text ( StandardStream stream, const QString& text )
	{
		if ( text.isEmpty () )
		{
			return true;
		}

		const HANDLE handle = handle_for ( stream );

		if ( !is_usable ( handle ) )
		{
			return false;
		}

		return is_console ( handle ) ? write_to_console ( handle, text ) : write_bytes ( handle, text.toUtf8 () );
	}

	QByteArray read_standard_input ()
	{
		const HANDLE handle = GetStdHandle ( STD_INPUT_HANDLE );

		if ( !is_usable ( handle ) )
		{
			return QByteArray ();
		}

		return is_console ( handle ) ? read_console ( handle ) : read_stream ( handle );
	}
}

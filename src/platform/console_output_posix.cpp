//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   console_output, POSIX -- see the header for the design. A terminal, a pipe and a file all take UTF-8 bytes here,
//   so the Windows half's choice of encoding has no counterpart. A program started from a desktop launcher still has
//   standard output -- it is simply not shown to anyone -- so there is no message-box case here either.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/console_output.hpp>

#include <cstdio>
#include <unistd.h>

namespace vje::platform
{
	namespace
	{
		constexpr std::size_t STANDARD_INPUT_CHUNK_BYTES = 65536;

		std::FILE* file_for ( StandardStream stream )
		{
			return ( stream == StandardStream::Output ) ? stdout : stderr;
		}
	}

	bool write_text ( StandardStream stream, const QString& text )
	{
		if ( text.isEmpty () )
		{
			return true;
		}

		const QByteArray bytes = text.toUtf8 ();
		std::FILE*       file  = file_for ( stream );

		const std::size_t written = std::fwrite ( bytes.constData (), 1, static_cast<std::size_t> ( bytes.size () ), file );

		return ( std::fflush ( file ) == 0 ) && ( written == static_cast<std::size_t> ( bytes.size () ) );
	}

	QByteArray read_standard_input ()
	{
		QByteArray bytes;
		char       chunk [ STANDARD_INPUT_CHUNK_BYTES ];

		for ( ;; )
		{
			const ssize_t read = ::read ( STDIN_FILENO, chunk, sizeof ( chunk ) );

			if ( read <= 0 )
			{
				break;
			}

			bytes.append ( chunk, static_cast<qsizetype> ( read ) );
		}

		return bytes;
	}
}

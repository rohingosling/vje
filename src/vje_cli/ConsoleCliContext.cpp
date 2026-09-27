//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ConsoleCliContext implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/ConsoleCliContext.hpp>

namespace vje::cli
{
	void ConsoleCliContext::write_output ( const QString& text )
	{
		deliver ( platform::StandardStream::Output, text );
	}

	void ConsoleCliContext::write_error ( const QString& text )
	{
		deliver ( platform::StandardStream::Error, text );
	}

	QByteArray ConsoleCliContext::read_standard_input ()
	{
		return platform::read_standard_input ();
	}

	const QString& ConsoleCliContext::undelivered_text () const
	{
		return undelivered;
	}

	void ConsoleCliContext::deliver ( platform::StandardStream stream, const QString& text )
	{
		// Whatever the platform could not deliver is kept -- decided by the write itself, the one answer that cannot be
		// wrong about whether a handle leads anywhere (platform/console_output.hpp).

		if ( !platform::write_text ( stream, text ) )
		{
			undelivered += text;
		}
	}
}

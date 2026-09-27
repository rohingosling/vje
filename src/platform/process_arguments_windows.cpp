//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   process_arguments, Windows -- see the header for the design.
//
//   THE COUNT CHECK. CommandLineToArgvW and the C runtime split a command line by slightly different rules around
//   quotes and backslashes, so on an exotic line the two can disagree about how many arguments there are. When they
//   do, pairing the wide list with argv index by index would hand an argument's text to the wrong position, which is
//   worse than a mis-decoded character. So a disagreement falls back to argv entire. For vje.exe the two cannot
//   disagree at all -- Qt's WinMain builds argv FROM CommandLineToArgvW -- so the check exists for vje.com, whose argv
//   comes from the runtime.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/process_arguments.hpp>

#include <windows.h>
#include <shellapi.h>

namespace vje::platform
{
	QStringList command_line_arguments ( int argc, char* argv [] )
	{
		int     wideCount     = 0;
		LPWSTR* wideArguments = CommandLineToArgvW ( GetCommandLineW (), &wideCount );

		QStringList arguments;

		if ( ( wideArguments != nullptr ) && ( wideCount == argc ) )
		{
			for ( int i = 0; i < wideCount; ++i )
			{
				arguments.append ( QString::fromWCharArray ( wideArguments [ i ] ) );
			}
		}
		else
		{
			for ( int i = 0; i < argc; ++i )
			{
				arguments.append ( QString::fromLocal8Bit ( argv [ i ] ) );
			}
		}

		if ( wideArguments != nullptr )
		{
			LocalFree ( wideArguments );
		}

		return arguments;
	}
}

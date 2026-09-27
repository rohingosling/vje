//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   process_arguments, POSIX -- argv is the command line, in the locale's encoding, and fromLocal8Bit reads it the way
//   QCoreApplication::arguments () does. See the header for why this exists at all.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/process_arguments.hpp>

namespace vje::platform
{
	QStringList command_line_arguments ( int argc, char* argv [] )
	{
		QStringList arguments;

		for ( int i = 0; i < argc; ++i )
		{
			arguments.append ( QString::fromLocal8Bit ( argv [ i ] ) );
		}

		return arguments;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   document_input -- reading a file operand, or standard input for "-", into text (CLI-04..06).
//
//   IT READS THE BYTES ITSELF rather than calling DocumentIo::load_file, which reports "could not open" and "not JSON"
//   as one failure. The command line keeps them apart -- exit 3 against exit 1 -- because they call for different
//   action from whoever reads the code. The DECODING is still File > Open's, through DocumentIo::decode_text, so a
//   file cannot validate here and then fail to open in the window.
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
	class CliContext;

	struct DocumentInput
	{
		QString displayName;                               // The operand as given, or "<stdin>" for "-".
		bool    readable = false;
		QString problem;                                   // Why it could not be read, when it could not.
		QString text;                                      // The decoded text, when it could.
	};

	DocumentInput read_document ( const QString& operand, CliContext& context );
}

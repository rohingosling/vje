//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   document_input implementation -- see the header for the design.
//
//   THE TWO COMMON FAILURES ARE NAMED BY VJE, NOT BY THE OPERATING SYSTEM. QFile's error for a missing file is the
//   platform's own sentence ("The system cannot find the file specified." on Windows), which differs by platform and
//   by language; checking first gives both platforms the same words for the case a user meets most. A directory is
//   checked for the same reason and one more: on Linux QFile can open a directory, and reading it yields nothing,
//   which would report an empty -- and therefore invalid -- document rather than the mistake that was made.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/document_input.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/command_definitions.hpp>

#include <vje_core/services/DocumentIo.hpp>

#include <QFile>
#include <QFileInfo>

namespace vje::cli
{
	DocumentInput read_document ( const QString& operand, CliContext& context )
	{
		DocumentInput input;

		if ( operand == QLatin1String ( STANDARD_INPUT_ARGUMENT ) )
		{
			input.displayName = QString::fromLatin1 ( STANDARD_INPUT_NAME );
			input.readable    = true;
			input.text        = DocumentIo::decode_text ( context.read_standard_input () );

			return input;
		}

		input.displayName = operand;

		const QFileInfo info ( operand );

		if ( !info.exists () )
		{
			input.problem = QStringLiteral ( "no such file" );
			return input;
		}

		if ( info.isDir () )
		{
			input.problem = QStringLiteral ( "it is a directory" );
			return input;
		}

		QFile file ( operand );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			input.problem = file.errorString ();
			return input;
		}

		input.readable = true;
		input.text     = DocumentIo::decode_text ( file.readAll () );

		return input;
	}
}

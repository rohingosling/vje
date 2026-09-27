//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ValidateCommand implementation -- see the header for the design.
//
//   ONE WRITE PER FILE, not one at the end. A report on a hundred files is read as it arrives, and a script piping it
//   through a filter sees each line as the file is checked rather than all of them after the last.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/ValidateCommand.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/commands/document_input.hpp>

#include <vje_core/services/JsonSerializer.hpp>
#include <vje_core/services/Validator.hpp>

namespace vje::cli
{
	QCommandLineOption ValidateCommand::trigger () const
	{
		return QCommandLineOption ( QStringLiteral ( "validate" ) );
	}

	QString ValidateCommand::operands () const
	{
		return QStringLiteral ( "<file>..." );
	}

	QString ValidateCommand::summary () const
	{
		return QStringLiteral ( "Check that each file is valid JSON." );
	}

	bool ValidateCommand::accepts_standard_input () const
	{
		return true;
	}

	QString ValidateCommand::check_arguments ( const ParsedCommandLine& commandLine ) const
	{
		if ( commandLine.positionals.isEmpty () )
		{
			return QStringLiteral ( "--validate needs at least one file ('%1' for standard input)." )
			           .arg ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ) );
		}

		// Standard input can be read once. A second "-" would read nothing and report an empty -- and so invalid --
		// document that the user never gave.

		if ( commandLine.positionals.count ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ) ) > 1 )
		{
			return QStringLiteral ( "Standard input ('%1') can be read only once." )
			           .arg ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ) );
		}

		if ( commandLine.positionals.contains ( QString () ) )
		{
			return QStringLiteral ( "A file name is empty." );
		}

		return QString ();
	}

	ExitCode ValidateCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( registry )

		ExitCode outcome = ExitCode::Success;

		for ( const QString& operand : commandLine.positionals )
		{
			const DocumentInput input = read_document ( operand, context );

			if ( !input.readable )
			{
				context.write_output ( QStringLiteral ( "%1: error: cannot be read: %2\n" ).arg ( input.displayName, input.problem ) );

				outcome = worse_of ( outcome, ExitCode::Failure );
				continue;
			}

			const ValidationResult result = Validator::validate ( input.text );

			QString report;

			if ( result.ok )
			{
				report = QStringLiteral ( "%1: valid\n" ).arg ( input.displayName );
			}
			else
			{
				// ONE multi-argument arg () rather than a chain: a chain substitutes into what the previous call
				// inserted, so a file named "a%2.json" would have its "%2" replaced by the line number.

				report = QStringLiteral ( "%1:%2:%3: error: %4\n" )
				             .arg ( input.displayName,
				                    QString::number ( result.issue.line ),
				                    QString::number ( result.issue.column ),
				                    result.issue.message );

				outcome = worse_of ( outcome, ExitCode::Negative );
			}

			// Duplicates exist only in a document that parsed, so they follow "valid" and never an error. The key is
			// written as a JSON string literal, so a key containing a quote or a line break stays on one line and is
			// unambiguous.

			for ( const DuplicateKey& duplicate : result.duplicateKeys )
			{
				report += QStringLiteral ( "%1:%2:%3: warning: duplicate key %4\n" )
				              .arg ( input.displayName,
				                     QString::number ( duplicate.line ),
				                     QString::number ( duplicate.column ),
				                     JsonSerializer::encode_string ( duplicate.key ) );
			}

			context.write_output ( report );
		}

		return outcome;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   QueryCommand implementation -- see the header for the design.
//
//   THE EXPRESSION IS COMPILED BEFORE THE FILE IS READ. A malformed query is a usage error whatever the file holds,
//   and compiling first means a typo in the query is reported as a typo in the query -- not hidden behind a slow read
//   of a large file, and not behind a complaint about a file that does not exist.
//
//   THE WHOLE RESULT IS WRITTEN IN ONE CALL. A query over a large document can match hundreds of thousands of nodes,
//   and one write per line is one system call per line.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/QueryCommand.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/commands/document_input.hpp>

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/DocumentIo.hpp>
#include <vje_core/services/JsonPathQuery.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <QStringList>

namespace vje::cli
{
	namespace
	{
		constexpr auto QUERY_OPTION  = "query";
		constexpr auto VALUES_OPTION = "values";

		// The malformed query, with a caret under the column the engine named -- QUERY-05's "selects the offending
		// span", in the only form a terminal has. A multi-line query gets no caret: a caret under line one would point
		// at the wrong character, and the column in the message is still exact.

		QString describe_query_error ( const QString& expression, const JsonPathError& error )
		{
			QString text = QStringLiteral ( "%1: --query: column %2: %3\n" )
			                   .arg ( QString::fromLatin1 ( PROGRAM_NAME ), QString::number ( error.position + 1 ), error.message );

			if ( !expression.contains ( QLatin1Char ( '\n' ) ) && !expression.contains ( QLatin1Char ( '\r' ) ) )
			{
				text += QStringLiteral ( "  " ) + expression + QLatin1Char ( '\n' )
				      + QStringLiteral ( "  " ) + QString ( error.position, QLatin1Char ( ' ' ) ) + QLatin1Char ( '^' ) + QLatin1Char ( '\n' );
			}

			return text;
		}
	}

	QCommandLineOption QueryCommand::trigger () const
	{
		return QCommandLineOption ( QString::fromLatin1 ( QUERY_OPTION ), QString (), QStringLiteral ( "expression" ) );
	}

	QList<QCommandLineOption> QueryCommand::modifiers () const
	{
		return
		{
			QCommandLineOption ( QString::fromLatin1 ( VALUES_OPTION ),
			                     QStringLiteral ( "With --query, print each result's value as one line of compact JSON instead of its JSON Pointer." ) )
		};
	}

	QString QueryCommand::operands () const
	{
		return QStringLiteral ( "<file>" );
	}

	QString QueryCommand::summary () const
	{
		return QStringLiteral ( "Run a JSONPath query and print the results." );
	}

	bool QueryCommand::accepts_standard_input () const
	{
		return true;
	}

	QString QueryCommand::check_arguments ( const ParsedCommandLine& commandLine ) const
	{
		const qsizetype fileCount = commandLine.positionals.size ();

		if ( fileCount == 0 )
		{
			return QStringLiteral ( "--query needs a file to read ('%1' for standard input)." )
			           .arg ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ) );
		}

		if ( fileCount > 1 )
		{
			return QStringLiteral ( "--query reads one file; %1 were given." ).arg ( fileCount );
		}

		if ( commandLine.positionals.front ().isEmpty () )
		{
			return QStringLiteral ( "The file name is empty." );
		}

		return QString ();
	}

	ExitCode QueryCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( registry )

		const QString       expression = commandLine.value ( QString::fromLatin1 ( QUERY_OPTION ) );
		const JsonPathQuery query      = JsonPathQuery::compile ( expression );

		if ( !query.is_valid () )
		{
			context.write_error ( describe_query_error ( expression, query.error () ) );

			return ExitCode::UsageError;
		}

		const DocumentInput input = read_document ( commandLine.positionals.front (), context );

		if ( !input.readable )
		{
			context.write_error ( QStringLiteral ( "%1: %2: cannot be read: %3\n" )
			                          .arg ( QString::fromLatin1 ( PROGRAM_NAME ), input.displayName, input.problem ) );

			return ExitCode::Failure;
		}

		const LoadResult loaded = DocumentIo::load_text ( input.text );

		if ( !loaded.ok )
		{
			// Exit 3, not 1: "the query matched nothing" is an answer about the document, and there is no document.

			context.write_error ( QStringLiteral ( "%1: %2:%3:%4: not valid JSON: %5\n" )
			                          .arg ( QString::fromLatin1 ( PROGRAM_NAME ),
			                                 input.displayName,
			                                 QString::number ( loaded.error.line ),
			                                 QString::number ( loaded.error.column ),
			                                 loaded.error.message ) );

			return ExitCode::Failure;
		}

		const bool                     printValues = commandLine.is_set ( QString::fromLatin1 ( VALUES_OPTION ) );
		const std::vector<JsonPointer> results     = query.evaluate ( *loaded.root );

		QString output;

		for ( const JsonPointer& pointer : results )
		{
			if ( printValues )
			{
				// The pointer resolves by construction -- the engine produced it from this very tree -- and a pointer
				// to the second of two duplicate keys carries which occurrence it names (15h.5), so the value printed is
				// the one matched even where the POINTER text cannot say which (CLI-05).

				const JsonNode* node = pointer.resolve ( loaded.root.get () );

				output += ( ( node != nullptr ) ? JsonSerializer::serialize ( *node ) : QString () ) + QLatin1Char ( '\n' );
			}
			else
			{
				output += pointer.to_string () + QLatin1Char ( '\n' );
			}
		}

		context.write_output ( output );

		return results.empty () ? ExitCode::Negative : ExitCode::Success;
	}
}

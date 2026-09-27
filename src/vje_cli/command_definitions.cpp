//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   command_definitions implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/command_definitions.hpp>

#include <vje_cli/CommandRegistry.hpp>

#include <vje_core/version.hpp>

#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace vje::cli
{
	namespace
	{
		// The window launch: the one positional argument, stated once for the help. The parser needs no statement of
		// it -- QCommandLineParser hands every positional back whatever it was told -- and plan_launch enforces the
		// count, because "at most one file" is a rule about the WINDOW and not about parsing.

		constexpr auto WINDOW_OPERANDS = "[<file>]";
		constexpr auto WINDOW_SUMMARY  = "Open the window, with <file> loaded if given.";

		// Qt's documented window options (QGuiApplication, QApplication, and the X11 pair). Those taking a value first.

		constexpr const char* TOOLKIT_VALUE_OPTIONS [] =
		{
			"platform",
			"platformpluginpath",
			"platformtheme",
			"plugin",
			"qmljsdebugger",
			"qwindowgeometry",
			"qwindowicon",
			"qwindowtitle",
			"session",
			"style",
			"stylesheet",
			"display",
			"geometry"
		};

		constexpr const char* TOOLKIT_FLAG_OPTIONS [] =
		{
			"reverse",
			"widgetcount"
		};

		// The exit codes as the help states them -- spec section 2.13's table, shortened to a line each.

		const std::pair<int, const char*> EXIT_CODE_LINES [] =
		{
			{ 0, "Success: every file is valid, the query matched at least one node, or the command completed." },
			{ 1, "A negative answer: a file is invalid, or the query matched nothing." },
			{ 2, "A usage error, a malformed JSONPath expression included." },
			{ 3, "A failure: a file could not be read or, for a query, is not valid JSON; or the command could not complete." }
		};

		// The gap between the synopsis column and the summary column.

		constexpr int COLUMN_GAP = 2;

		QString spelled_name ( const QString& name )
		{
			// One character is a short option; anything longer a long one. With ParseAsLongOptions the parser accepts
			// a long name after one dash too, but the help writes the conventional spelling.

			return ( ( name.size () == 1 ) ? QStringLiteral ( "-" ) : QStringLiteral ( "--" ) ) + name;
		}

		QString padded ( const QString& text, int width )
		{
			return text + QString ( width - text.size (), QLatin1Char ( ' ' ) );
		}

		QString command_synopsis ( const ICliCommand& command )
		{
			QString synopsis = option_synopsis ( command.trigger () );

			for ( const QCommandLineOption& modifier : command.modifiers () )
			{
				synopsis += QStringLiteral ( " [" ) + option_synopsis ( modifier ) + QStringLiteral ( "]" );
			}

			if ( !command.operands ().isEmpty () )
			{
				synopsis += QLatin1Char ( ' ' ) + command.operands ();
			}

			return QString::fromLatin1 ( PROGRAM_NAME ) + QLatin1Char ( ' ' ) + synopsis;
		}
	}

	QList<QCommandLineOption> toolkit_window_options ()
	{
		QList<QCommandLineOption> options;

		for ( const char* name : TOOLKIT_VALUE_OPTIONS )
		{
			QCommandLineOption option ( QString::fromLatin1 ( name ), QString (), QStringLiteral ( "value" ) );

			option.setFlags ( QCommandLineOption::HiddenFromHelp );
			options.append ( option );
		}

		for ( const char* name : TOOLKIT_FLAG_OPTIONS )
		{
			QCommandLineOption option ( QString::fromLatin1 ( name ) );

			option.setFlags ( QCommandLineOption::HiddenFromHelp );
			options.append ( option );
		}

		return options;
	}

	void configure_parser ( QCommandLineParser& parser, const CommandRegistry& registry )
	{
		parser.setSingleDashWordOptionMode ( QCommandLineParser::ParseAsLongOptions );

		for ( const auto& command : registry.commands () )
		{
			parser.addOption  ( command->trigger () );
			parser.addOptions ( command->modifiers () );
		}

		parser.addOptions ( toolkit_window_options () );
	}

	QString option_synopsis ( const QCommandLineOption& option )
	{
		QStringList spellings;

		for ( const QString& name : option.names () )
		{
			spellings.append ( spelled_name ( name ) );
		}

		QString synopsis = spellings.join ( QStringLiteral ( " | " ) );

		if ( !option.valueName ().isEmpty () )
		{
			synopsis += QStringLiteral ( " <" ) + option.valueName () + QStringLiteral ( ">" );
		}

		return synopsis;
	}

	QString help_text ( const CommandRegistry& registry )
	{
		// The usage block: the window launch, then every command, as synopsis and summary in two aligned columns.

		std::vector<std::pair<QString, QString>> usage;

		usage.emplace_back ( QString::fromLatin1 ( PROGRAM_NAME ) + QLatin1Char ( ' ' ) + QString::fromLatin1 ( WINDOW_OPERANDS ),
		                     QString::fromLatin1 ( WINDOW_SUMMARY ) );

		std::vector<std::pair<QString, QString>> modifierLines;
		QStringList                              standardInputReaders;

		for ( const auto& command : registry.commands () )
		{
			usage.emplace_back ( command_synopsis ( *command ), command->summary () );

			for ( const QCommandLineOption& modifier : command->modifiers () )
			{
				modifierLines.emplace_back ( option_synopsis ( modifier ), modifier.description () );
			}

			if ( command->accepts_standard_input () )
			{
				standardInputReaders.append ( spelled_name ( canonical_name ( command->trigger () ) ) );
			}
		}

		int usageWidth = 0;

		for ( const auto& line : usage )
		{
			usageWidth = std::max ( usageWidth, static_cast<int> ( line.first.size () ) );
		}

		QString text = version_line () + QStringLiteral ( "\n\nUsage:\n" );

		for ( const auto& line : usage )
		{
			text += QStringLiteral ( "  " ) + padded ( line.first, usageWidth + COLUMN_GAP ) + line.second + QLatin1Char ( '\n' );
		}

		// The options that belong to one command. Stated with their descriptions, because a synopsis can say that
		// --values exists but not what it does.

		if ( !modifierLines.empty () )
		{
			int modifierWidth = 0;

			for ( const auto& line : modifierLines )
			{
				modifierWidth = std::max ( modifierWidth, static_cast<int> ( line.first.size () ) );
			}

			text += QStringLiteral ( "\nOptions:\n" );

			for ( const auto& line : modifierLines )
			{
				text += QStringLiteral ( "  " ) + padded ( line.first, modifierWidth + COLUMN_GAP ) + line.second + QLatin1Char ( '\n' );
			}
		}

		if ( !standardInputReaders.isEmpty () )
		{
			text += QStringLiteral ( "\nA <file> of %1 reads standard input (%2).\n" )
			            .arg ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ), standardInputReaders.join ( QStringLiteral ( " and " ) ) );
		}

		text += QStringLiteral ( "\nExit codes:\n" );

		for ( const auto& line : EXIT_CODE_LINES )
		{
			text += QStringLiteral ( "  %1  %2\n" ).arg ( line.first ).arg ( QString::fromLatin1 ( line.second ) );
		}

		return text;
	}

	QString usage_error_text ( const QString& problem )
	{
		return QStringLiteral ( "%1: %2\nTry '%1 --help' for more information.\n" )
		           .arg ( QString::fromLatin1 ( PROGRAM_NAME ), problem );
	}
}

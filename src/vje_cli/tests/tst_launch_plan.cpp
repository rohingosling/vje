//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for launch_plan and the one definition it is built from (CLI-01..03, CLI-06, spec section 2.13) --
//   HEADLESS, and carrying most of the phase: what an invocation MEANS is decided here, before any process, window or
//   file is involved.
//
//   THE HAND-WRITTEN LOOP'S DEFECTS EACH HAVE A CASE. "-platform offscreen" opened a file called "offscreen"; an
//   unknown option was silently ignored; a second file was silently ignored. Each is asserted here as its opposite.
//
//   CLI-01 IS ASSERTED BOTH WAYS. Every option the parser accepts appears in the help, AND every option spelling the
//   help shows is accepted -- a one-way check would pass against a help that listed an option nothing parses.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/CommandRegistry.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/launch_plan.hpp>

#include <QRegularExpression>
#include <QSet>
#include <QtTest/QtTest>

using namespace vje::cli;

namespace
{
	const CommandRegistry& registry ()
	{
		return CommandRegistry::standard ();
	}

	LaunchPlan plan ( const QStringList& argumentsAfterProgram )
	{
		return plan_launch ( QStringList { QStringLiteral ( "vje" ) } + argumentsAfterProgram, registry () );
	}

	QString command_name ( const LaunchPlan& launchPlan )
	{
		return ( launchPlan.command != nullptr ) ? canonical_name ( launchPlan.command->trigger () ) : QString ();
	}
}

class TestLaunchPlan : public QObject
{
	Q_OBJECT

private slots:

	// The window.

	void no_arguments_open_the_window_with_no_file ();
	void one_file_opens_the_window_with_it ();
	void two_files_are_a_usage_error_rather_than_the_second_being_ignored ();
	void a_non_ascii_file_name_reaches_the_window_intact ();
	void the_toolkit_s_window_options_are_not_taken_for_a_file ();
	void a_double_dash_ends_the_options ();
	void standard_input_is_not_a_file_the_window_can_open ();
	void an_empty_file_name_is_a_usage_error ();

	// Commands.

	void each_command_is_recognized_by_every_spelling_data ();
	void each_command_is_recognized_by_every_spelling ();
	void two_commands_are_a_usage_error ();
	void a_command_beside_a_toolkit_option_is_a_usage_error ();
	void validate_takes_one_or_more_files ();
	void validate_needs_a_file ();
	void standard_input_is_a_file_for_the_commands ();
	void standard_input_can_be_read_only_once ();
	void query_takes_an_expression_and_exactly_one_file ();
	void query_without_a_file_is_a_usage_error ();
	void query_with_two_files_is_a_usage_error ();
	void query_without_an_expression_is_a_usage_error ();
	void values_belongs_to_query ();
	void values_without_query_is_a_usage_error ();
	void values_beside_another_command_is_a_usage_error ();

	// Usage errors.

	void an_unknown_option_is_a_usage_error_naming_it_data ();
	void an_unknown_option_is_a_usage_error_naming_it ();
	void only_usage_errors_and_informational_commands_are_answers ();

	// The definition (CLI-01).

	void every_option_the_parser_accepts_appears_in_the_help ();
	void every_option_the_help_shows_is_accepted ();
	void the_toolkit_s_options_are_accepted_but_never_documented ();
	void the_command_set_is_the_one_this_platform_offers ();
};

//=====================================================================================================================
// The window
//=====================================================================================================================

void TestLaunchPlan::no_arguments_open_the_window_with_no_file ()
{
	const LaunchPlan launchPlan = plan ( {} );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::OpenWindow );
	QVERIFY  ( launchPlan.file.isEmpty () );
}

void TestLaunchPlan::one_file_opens_the_window_with_it ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "sample.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::OpenWindow );
	QCOMPARE ( launchPlan.file, QStringLiteral ( "sample.json" ) );
}

void TestLaunchPlan::two_files_are_a_usage_error_rather_than_the_second_being_ignored ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "a.json" ), QStringLiteral ( "b.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "one file" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::a_non_ascii_file_name_reaches_the_window_intact ()
{
	// The plan holds whatever it was given, so the Unicode is only as good as platform::command_line_arguments -- which
	// tst_cli_process checks through a real process. What this pins is that nothing in between re-encodes it.

	const QString name = QStringLiteral ( "données-ñ-日本.json" );

	const LaunchPlan launchPlan = plan ( { name } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::OpenWindow );
	QCOMPARE ( launchPlan.file, name );
}

void TestLaunchPlan::the_toolkit_s_window_options_are_not_taken_for_a_file ()
{
	// THE DEFECT: the old loop examined only a leading dash, so "offscreen" -- the VALUE of -platform -- was opened as
	// a file. Two spellings, with the file on either side, since options and positionals may interleave.

	const LaunchPlan before = plan ( { QStringLiteral ( "-platform" ), QStringLiteral ( "offscreen" ), QStringLiteral ( "sample.json" ) } );

	QCOMPARE ( before.kind, LaunchPlan::Kind::OpenWindow );
	QCOMPARE ( before.file, QStringLiteral ( "sample.json" ) );

	const LaunchPlan after = plan ( { QStringLiteral ( "sample.json" ), QStringLiteral ( "-style" ), QStringLiteral ( "fusion" ) } );

	QCOMPARE ( after.kind, LaunchPlan::Kind::OpenWindow );
	QCOMPARE ( after.file, QStringLiteral ( "sample.json" ) );

	// And with no file at all, the value still is not one.

	const LaunchPlan none = plan ( { QStringLiteral ( "-platform" ), QStringLiteral ( "offscreen" ) } );

	QCOMPARE ( none.kind, LaunchPlan::Kind::OpenWindow );
	QVERIFY  ( none.file.isEmpty () );
}

void TestLaunchPlan::a_double_dash_ends_the_options ()
{
	// A file whose name begins with a dash is reachable, the conventional way.

	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--" ), QStringLiteral ( "--odd-name.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::OpenWindow );
	QCOMPARE ( launchPlan.file, QStringLiteral ( "--odd-name.json" ) );
}

void TestLaunchPlan::standard_input_is_not_a_file_the_window_can_open ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "-" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "standard input" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::an_empty_file_name_is_a_usage_error ()
{
	QCOMPARE ( plan ( { QString () } ).kind, LaunchPlan::Kind::UsageError );
	QCOMPARE ( plan ( { QStringLiteral ( "--validate" ), QString () } ).kind, LaunchPlan::Kind::UsageError );
	QCOMPARE ( plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), QString () } ).kind, LaunchPlan::Kind::UsageError );
}

//=====================================================================================================================
// Commands
//=====================================================================================================================

void TestLaunchPlan::each_command_is_recognized_by_every_spelling_data ()
{
	QTest::addColumn<QStringList> ( "arguments" );
	QTest::addColumn<QString>     ( "command" );

	QTest::newRow ( "-h" )        << QStringList { QStringLiteral ( "-h" ) }        << QStringLiteral ( "help" );
	QTest::newRow ( "--help" )    << QStringList { QStringLiteral ( "--help" ) }    << QStringLiteral ( "help" );
	QTest::newRow ( "-v" )        << QStringList { QStringLiteral ( "-v" ) }        << QStringLiteral ( "version" );
	QTest::newRow ( "--version" ) << QStringList { QStringLiteral ( "--version" ) } << QStringLiteral ( "version" );

	QTest::newRow ( "--validate" )
	    << QStringList { QStringLiteral ( "--validate" ), QStringLiteral ( "a.json" ) } << QStringLiteral ( "validate" );

	QTest::newRow ( "--query" )
	    << QStringList { QStringLiteral ( "--query" ), QStringLiteral ( "$..id" ), QStringLiteral ( "a.json" ) } << QStringLiteral ( "query" );

	QTest::newRow ( "--query=" )
	    << QStringList { QStringLiteral ( "--query=$..id" ), QStringLiteral ( "a.json" ) } << QStringLiteral ( "query" );
}

void TestLaunchPlan::each_command_is_recognized_by_every_spelling ()
{
	QFETCH ( QStringList, arguments );
	QFETCH ( QString,     command );

	const LaunchPlan launchPlan = plan ( arguments );

	QVERIFY2 ( launchPlan.kind == LaunchPlan::Kind::RunCommand, qPrintable ( launchPlan.problem ) );
	QCOMPARE ( command_name ( launchPlan ), command );
}

void TestLaunchPlan::two_commands_are_a_usage_error ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--help" ), QStringLiteral ( "--validate" ), QStringLiteral ( "a.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "one command" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::a_command_beside_a_toolkit_option_is_a_usage_error ()
{
	// The toolkit's options configure a window, and a command opens none -- refused rather than silently ignored.

	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "a.json" ),
	                                       QStringLiteral ( "-platform" ), QStringLiteral ( "offscreen" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "-platform" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::validate_takes_one_or_more_files ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "a.json" ),
	                                       QStringLiteral ( "b.json" ), QStringLiteral ( "c.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::RunCommand );
	QCOMPARE ( launchPlan.commandLine.positionals,
	           ( QStringList { QStringLiteral ( "a.json" ), QStringLiteral ( "b.json" ), QStringLiteral ( "c.json" ) } ) );
}

void TestLaunchPlan::validate_needs_a_file ()
{
	QCOMPARE ( plan ( { QStringLiteral ( "--validate" ) } ).kind, LaunchPlan::Kind::UsageError );
}

void TestLaunchPlan::standard_input_is_a_file_for_the_commands ()
{
	const LaunchPlan validate = plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "-" ) } );

	QCOMPARE ( validate.kind, LaunchPlan::Kind::RunCommand );
	QCOMPARE ( validate.commandLine.positionals, QStringList { QStringLiteral ( "-" ) } );

	const LaunchPlan query = plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), QStringLiteral ( "-" ) } );

	QCOMPARE ( query.kind, LaunchPlan::Kind::RunCommand );
	QCOMPARE ( query.commandLine.positionals, QStringList { QStringLiteral ( "-" ) } );
}

void TestLaunchPlan::standard_input_can_be_read_only_once ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "-" ), QStringLiteral ( "-" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "only once" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::query_takes_an_expression_and_exactly_one_file ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$..id" ), QStringLiteral ( "a.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::RunCommand );
	QCOMPARE ( launchPlan.commandLine.value ( QStringLiteral ( "query" ) ), QStringLiteral ( "$..id" ) );
	QCOMPARE ( launchPlan.commandLine.positionals, QStringList { QStringLiteral ( "a.json" ) } );
	QVERIFY  ( !launchPlan.commandLine.is_set ( QStringLiteral ( "values" ) ) );
}

void TestLaunchPlan::query_without_a_file_is_a_usage_error ()
{
	QCOMPARE ( plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ) } ).kind, LaunchPlan::Kind::UsageError );
}

void TestLaunchPlan::query_with_two_files_is_a_usage_error ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ),
	                                       QStringLiteral ( "a.json" ), QStringLiteral ( "b.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "one file" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::query_without_an_expression_is_a_usage_error ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--query" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "query" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::values_belongs_to_query ()
{
	// Either side of the file: options and positionals interleave.

	for ( const QStringList& arguments : { QStringList { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), QStringLiteral ( "--values" ), QStringLiteral ( "a.json" ) },
	                                       QStringList { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), QStringLiteral ( "a.json" ), QStringLiteral ( "--values" ) } } )
	{
		const LaunchPlan launchPlan = plan ( arguments );

		QVERIFY2 ( launchPlan.kind == LaunchPlan::Kind::RunCommand, qPrintable ( launchPlan.problem ) );
		QVERIFY  ( launchPlan.commandLine.is_set ( QStringLiteral ( "values" ) ) );
		QCOMPARE ( launchPlan.commandLine.positionals, QStringList { QStringLiteral ( "a.json" ) } );
	}
}

void TestLaunchPlan::values_without_query_is_a_usage_error ()
{
	// With no command this would otherwise be a window launch that ignored --values.

	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--values" ), QStringLiteral ( "a.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "--query" ) ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::values_beside_another_command_is_a_usage_error ()
{
	const LaunchPlan launchPlan = plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "--values" ), QStringLiteral ( "a.json" ) } );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( QStringLiteral ( "--values" ) ), qPrintable ( launchPlan.problem ) );
}

//=====================================================================================================================
// Usage errors
//=====================================================================================================================

void TestLaunchPlan::an_unknown_option_is_a_usage_error_naming_it_data ()
{
	QTest::addColumn<QStringList> ( "arguments" );
	QTest::addColumn<QString>     ( "named" );

	// Double dash, single dash, and beside a real command -- the old loop ignored all three.

	QTest::newRow ( "--bogus" )        << QStringList { QStringLiteral ( "--bogus" ) }                                      << QStringLiteral ( "bogus" );
	QTest::newRow ( "-bogus" )         << QStringList { QStringLiteral ( "-bogus" ), QStringLiteral ( "a.json" ) }          << QStringLiteral ( "bogus" );
	QTest::newRow ( "with a command" ) << QStringList { QStringLiteral ( "--validate" ), QStringLiteral ( "--strict" ), QStringLiteral ( "a.json" ) } << QStringLiteral ( "strict" );
}

void TestLaunchPlan::an_unknown_option_is_a_usage_error_naming_it ()
{
	QFETCH ( QStringList, arguments );
	QFETCH ( QString,     named );

	const LaunchPlan launchPlan = plan ( arguments );

	QCOMPARE ( launchPlan.kind, LaunchPlan::Kind::UsageError );
	QVERIFY2 ( launchPlan.problem.contains ( named ), qPrintable ( launchPlan.problem ) );
}

void TestLaunchPlan::only_usage_errors_and_informational_commands_are_answers ()
{
	// The rule the window program's message box follows (CLI-07).

	QVERIFY  ( output_is_the_answer ( plan ( { QStringLiteral ( "--help" ) } ) ) );
	QVERIFY  ( output_is_the_answer ( plan ( { QStringLiteral ( "--version" ) } ) ) );
	QVERIFY  ( output_is_the_answer ( plan ( { QStringLiteral ( "--bogus" ) } ) ) );

	QVERIFY  ( !output_is_the_answer ( plan ( { QStringLiteral ( "--validate" ), QStringLiteral ( "a.json" ) } ) ) );
	QVERIFY  ( !output_is_the_answer ( plan ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), QStringLiteral ( "a.json" ) } ) ) );
	QVERIFY  ( !output_is_the_answer ( plan ( { QStringLiteral ( "a.json" ) } ) ) );
}

//=====================================================================================================================
// The definition (CLI-01)
//=====================================================================================================================

void TestLaunchPlan::every_option_the_parser_accepts_appears_in_the_help ()
{
	QCommandLineParser parser;

	configure_parser ( parser, registry () );

	const QString help = help_text ( registry () );

	const QList<QCommandLineOption> toolkit = toolkit_window_options ();

	QSet<QString> toolkitNames;

	for ( const QCommandLineOption& option : toolkit )
	{
		toolkitNames.insert ( canonical_name ( option ) );
	}

	int documented = 0;

	for ( const auto& command : registry ().commands () )
	{
		for ( const QCommandLineOption& option : QList<QCommandLineOption> { command->trigger () } + command->modifiers () )
		{
			for ( const QString& name : option.names () )
			{
				const QString spelling = ( ( name.size () == 1 ) ? QStringLiteral ( "-" ) : QStringLiteral ( "--" ) ) + name;

				// As a whole word, so "--query" is not satisfied by some longer option that begins with it.

				const QRegularExpression word ( QStringLiteral ( "(^|[\\s\\[|])" ) + QRegularExpression::escape ( spelling ) + QStringLiteral ( "($|[\\s\\]=<])" ),
				                                QRegularExpression::MultilineOption );

				QVERIFY2 ( word.match ( help ).hasMatch (), qPrintable ( spelling + QStringLiteral ( " is accepted but not in the help:\n" ) + help ) );

				++documented;
			}

			QVERIFY ( !toolkitNames.contains ( canonical_name ( option ) ) );
		}
	}

	QVERIFY ( documented >= 6 );                           // -h --help -v --version --validate --query --values
}

void TestLaunchPlan::every_option_the_help_shows_is_accepted ()
{
	// The other direction: harvest every option spelling from the help text and parse each one. A spelling the parser
	// refuses as UNKNOWN is a documented option nothing accepts. (A missing value or a missing file is a different
	// complaint, and a fine one -- the option itself was recognised.)

	const QString help = help_text ( registry () );

	const QRegularExpression spellingPattern ( QStringLiteral ( "(?:^|[\\s\\[|])(--?[a-z][a-z-]*)" ) );

	QSet<QString> spellings;

	for ( auto match = spellingPattern.globalMatch ( help ); match.hasNext (); )
	{
		spellings.insert ( match.next ().captured ( 1 ) );
	}

	QVERIFY2 ( spellings.size () >= 7, qPrintable ( QStringList ( spellings.values () ).join ( QLatin1Char ( ' ' ) ) ) );

	for ( const QString& spelling : spellings )
	{
		const LaunchPlan launchPlan = plan ( { spelling } );

		QVERIFY2 ( !launchPlan.problem.contains ( QStringLiteral ( "Unknown option" ) ),
		           qPrintable ( spelling + QStringLiteral ( " is in the help but not accepted: " ) + launchPlan.problem ) );
	}
}

void TestLaunchPlan::the_toolkit_s_options_are_accepted_but_never_documented ()
{
	const QString help = help_text ( registry () );

	for ( const QCommandLineOption& option : toolkit_window_options () )
	{
		const QString name = canonical_name ( option );

		QVERIFY2 ( !help.contains ( QStringLiteral ( "-" ) + name ), qPrintable ( name ) );

		QStringList arguments { QStringLiteral ( "-" ) + name };

		if ( !option.valueName ().isEmpty () )
		{
			arguments.append ( QStringLiteral ( "value" ) );
		}

		const LaunchPlan launchPlan = plan ( arguments );

		QVERIFY2 ( launchPlan.kind == LaunchPlan::Kind::OpenWindow, qPrintable ( name + QStringLiteral ( ": " ) + launchPlan.problem ) );
		QVERIFY2 ( launchPlan.file.isEmpty (), qPrintable ( name ) );
	}
}

void TestLaunchPlan::the_command_set_is_the_one_this_platform_offers ()
{
	// The Explorer pair (CLI-08) is the one entry that differs: on Windows it sits between the two commands a user came
	// for and the two that describe VJE (spec section 2.13's order), and on Linux it is ABSENT -- from the registry,
	// and so from the parser and the help alike (CLI-01) -- rather than present and refused.

	QStringList names;

	for ( const auto& command : registry ().commands () )
	{
		QVERIFY ( command->offered_on_this_platform () );

		names.append ( canonical_name ( command->trigger () ) );
	}

	QStringList expected { QStringLiteral ( "validate" ), QStringLiteral ( "query" ) };

#if defined ( Q_OS_WIN )
	expected << QStringLiteral ( "register-explorer-integration" ) << QStringLiteral ( "unregister-explorer-integration" );
#endif

	expected << QStringLiteral ( "help" ) << QStringLiteral ( "version" );

	QCOMPARE ( names, expected );

	// Absent means a usage error where the platform lacks it, exactly as for any option nobody defined.

#if !defined ( Q_OS_WIN )
	QVERIFY ( plan ( { QStringLiteral ( "--register-explorer-integration" ) } ).kind == LaunchPlan::Kind::UsageError );
	QVERIFY ( !help_text ( registry () ).contains ( QStringLiteral ( "explorer" ) ) );
#else
	QVERIFY ( plan ( { QStringLiteral ( "--register-explorer-integration" ) } ).kind == LaunchPlan::Kind::RunCommand );
	QVERIFY ( plan ( { QStringLiteral ( "--unregister-explorer-integration" ) } ).kind == LaunchPlan::Kind::RunCommand );

	// Two commands at once is still a usage error, the pair included (CLI-03).

	QVERIFY ( plan ( { QStringLiteral ( "--register-explorer-integration" ), QStringLiteral ( "--unregister-explorer-integration" ) } ).kind
	          == LaunchPlan::Kind::UsageError );

	// And neither takes a file: the pair acts on the window program, not on a document.

	QVERIFY ( plan ( { QStringLiteral ( "--register-explorer-integration" ), QStringLiteral ( "a.json" ) } ).kind == LaunchPlan::Kind::UsageError );
#endif
}

QTEST_GUILESS_MAIN ( TestLaunchPlan )

#include "tst_launch_plan.moc"

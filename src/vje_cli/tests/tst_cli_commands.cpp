//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for the commands that run without a window (CLI-03..06, spec section 2.13) -- HEADLESS. Each case plans
//   a real command line and executes it against a capturing CliContext, so what is asserted is exactly what a script
//   would read: the text on each stream, and the exit code.
//
//   THE FILES ARE REAL, in a temporary directory, because telling "could not be read" (exit 3) apart from "not JSON"
//   (exit 1) is half of what --validate is for, and only a file system can produce the first.
//
//   THE EXPLORER PAIR (CLI-08) is run directly rather than planned, because each of the two is constructed with its
//   target -- a settings file in the temporary directory, a scratch registry root under HKEY_CURRENT_USER\Software
//   \VJE-Test, and a stand-in window program -- so no case reaches the user's settings or Explorer's real menus.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/CliContext.hpp>
#include <vje_cli/CommandRegistry.hpp>
#include <vje_cli/ConsoleCliContext.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/launch_plan.hpp>
#include <vje_cli/commands/ExplorerIntegrationCommands.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <platform/explorer_integration.hpp>

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/DocumentIo.hpp>
#include <vje_core/version.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#if defined ( Q_OS_WIN )
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace vje;
using namespace vje::cli;

namespace explorer = vje::platform::explorer_integration;

namespace
{
	// The Explorer pair's scratch root, under HKEY_CURRENT_USER. Deleted before and after every case.

	const QString EXPLORER_SCRATCH_TREE   = QStringLiteral ( "Software\\VJE-Test\\tst_cli_commands" );
	const QString EXPLORER_SCRATCH_PARENT = QStringLiteral ( "Software\\VJE-Test" );
	const QString EXPLORER_SCRATCH_ROOT   = EXPLORER_SCRATCH_TREE + QStringLiteral ( "\\Classes" );

	void delete_explorer_scratch_tree ()
	{
#if defined ( Q_OS_WIN )
		RegDeleteTreeW ( HKEY_CURRENT_USER, EXPLORER_SCRATCH_TREE.toStdWString ().c_str () );
		RegDeleteKeyW  ( HKEY_CURRENT_USER, EXPLORER_SCRATCH_TREE.toStdWString ().c_str () );
		RegDeleteKeyW  ( HKEY_CURRENT_USER, EXPLORER_SCRATCH_PARENT.toStdWString ().c_str () );
#endif
	}

	// What one of the pair returned and wrote.

	struct ExplorerOutcome
	{
		ExitCode code = ExitCode::Success;
		QString  output;
		QString  error;
	};
	class CapturingContext : public CliContext
	{
	public:

		void write_output ( const QString& text ) override
		{
			output += text;
		}

		void write_error ( const QString& text ) override
		{
			error += text;
		}

		QByteArray read_standard_input () override
		{
			++standardInputReads;

			return standardInput;
		}

		QString    output;
		QString    error;
		QByteArray standardInput;
		int        standardInputReads = 0;
	};

	struct Outcome
	{
		ExitCode code = ExitCode::Success;
		QString  output;
		QString  error;
	};

	Outcome run ( const QStringList& argumentsAfterProgram, const QByteArray& standardInput = QByteArray () )
	{
		const CommandRegistry& registry = CommandRegistry::standard ();
		const LaunchPlan       plan     = plan_launch ( QStringList { QStringLiteral ( "vje" ) } + argumentsAfterProgram, registry );

		CapturingContext context;
		context.standardInput = standardInput;

		Outcome outcome;
		outcome.code   = execute ( plan, registry, context );
		outcome.output = context.output;
		outcome.error  = context.error;

		return outcome;
	}

	QStringList lines_of ( const QString& text )
	{
		QStringList lines = text.split ( QLatin1Char ( '\n' ) );

		// Every report ends in a newline, so the split's last element is the empty remainder after it.

		if ( !lines.isEmpty () && lines.constLast ().isEmpty () )
		{
			lines.removeLast ();
		}

		return lines;
	}
}

class TestCliCommands : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// Help and version.

	void version_prints_the_version_line ();
	void help_prints_the_usage ();

	// Usage errors.

	void a_usage_error_goes_to_standard_error_and_points_at_help ();

	// --validate (CLI-04).

	void a_valid_file_reports_valid ();
	void an_invalid_file_reports_its_first_error_with_line_and_column ();
	void duplicate_keys_are_warnings_with_positions_and_do_not_fail ();
	void a_file_that_cannot_be_read_is_exit_three ();
	void a_directory_is_not_a_readable_file ();
	void several_files_return_the_highest_code_data ();
	void several_files_return_the_highest_code ();
	void a_byte_order_mark_is_tolerated ();
	void validate_reads_standard_input ();
	void a_file_name_with_a_percent_sign_is_printed_as_named ();

	// --query (CLI-05).

	void query_prints_pointers_in_document_order ();
	void query_pointers_round_trip_through_go_to ();
	void query_prints_the_root_as_an_empty_line ();
	void query_values_print_compact_json_with_number_tokens_as_written ();
	void a_query_matching_nothing_is_exit_one_with_no_output ();
	void a_malformed_query_names_its_column_and_is_exit_two ();
	void query_on_an_unreadable_file_is_exit_three ();
	void query_on_invalid_json_is_exit_three ();
	void query_reads_standard_input ();
	void a_command_reads_standard_input_only_when_asked ();

	// The real context (CLI-07).

	void text_the_platform_cannot_deliver_is_kept_for_the_message_box ();

	// The Explorer pair (CLI-08), against a scratch registry root. Windows only.

	void register_writes_the_entries_and_records_yes ();
	void unregister_removes_the_entries_and_records_no ();
	void unregister_with_nothing_registered_succeeds_and_writes_no_settings ();
	void register_refuses_a_window_program_that_is_not_there ();

private:

	ExplorerCommandTarget explorer_target () const;

	QString write_file ( const QString& name, const QByteArray& content );

	std::unique_ptr<QTemporaryDir> directory;
};

void TestCliCommands::init ()
{
	directory = std::make_unique<QTemporaryDir> ();

	QVERIFY ( directory->isValid () );

	delete_explorer_scratch_tree ();
}

void TestCliCommands::cleanup ()
{
	delete_explorer_scratch_tree ();
}

ExplorerCommandTarget TestCliCommands::explorer_target () const
{
	// Every member filled in, so nothing reaches the user's settings file or the live classes root.

	ExplorerCommandTarget target;

	target.settingsFilePath  = directory->filePath ( QStringLiteral ( "settings.json" ) );
	target.classesRoot       = EXPLORER_SCRATCH_ROOT;
	target.windowProgramPath = directory->filePath ( QStringLiteral ( "vje.exe" ) );

	return target;
}

QString TestCliCommands::write_file ( const QString& name, const QByteArray& content )
{
	const QString path = directory->filePath ( name );

	QFile file ( path );

	if ( !file.open ( QIODevice::WriteOnly ) )
	{
		return QString ();
	}

	file.write ( content );

	return path;
}

//=====================================================================================================================
// Help and version
//=====================================================================================================================

void TestCliCommands::version_prints_the_version_line ()
{
	for ( const QString& spelling : { QStringLiteral ( "--version" ), QStringLiteral ( "-v" ) } )
	{
		const Outcome outcome = run ( { spelling } );

		QCOMPARE ( outcome.code,   ExitCode::Success );
		QCOMPARE ( outcome.output, version_line () + QLatin1Char ( '\n' ) );
		QCOMPARE ( outcome.output, QStringLiteral ( "VJE 2.0.0\n" ) );
		QVERIFY  ( outcome.error.isEmpty () );
	}
}

void TestCliCommands::help_prints_the_usage ()
{
	const Outcome outcome = run ( { QStringLiteral ( "--help" ) } );

	QCOMPARE ( outcome.code,   ExitCode::Success );
	QCOMPARE ( outcome.output, help_text ( CommandRegistry::standard () ) );
	QVERIFY  ( outcome.output.contains ( QStringLiteral ( "vje --validate <file>..." ) ) );
	QVERIFY  ( outcome.output.contains ( QStringLiteral ( "vje --query <expression> [--values] <file>" ) ) );
	QVERIFY  ( outcome.output.contains ( QStringLiteral ( "vje [<file>]" ) ) );
	QVERIFY  ( outcome.error.isEmpty () );
}

//=====================================================================================================================
// Usage errors
//=====================================================================================================================

void TestCliCommands::a_usage_error_goes_to_standard_error_and_points_at_help ()
{
	const Outcome outcome = run ( { QStringLiteral ( "--bogus" ) } );

	QCOMPARE ( outcome.code, ExitCode::UsageError );
	QVERIFY  ( outcome.output.isEmpty () );
	QVERIFY2 ( outcome.error.startsWith ( QStringLiteral ( "vje: " ) ),                         qPrintable ( outcome.error ) );
	QVERIFY2 ( outcome.error.contains   ( QStringLiteral ( "bogus" ) ),                         qPrintable ( outcome.error ) );
	QVERIFY2 ( outcome.error.contains   ( QStringLiteral ( "Try 'vje --help'" ) ),              qPrintable ( outcome.error ) );
}

//=====================================================================================================================
// --validate
//=====================================================================================================================

void TestCliCommands::a_valid_file_reports_valid ()
{
	const QString path = write_file ( QStringLiteral ( "good.json" ), "{ \"a\": [ 1, 2, 3 ] }\n" );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code,   ExitCode::Success );
	QCOMPARE ( outcome.output, path + QStringLiteral ( ": valid\n" ) );
	QVERIFY  ( outcome.error.isEmpty () );
}

void TestCliCommands::an_invalid_file_reports_its_first_error_with_line_and_column ()
{
	// The error is on line 3, at the "}" that follows a trailing comma -- two positions a reader can check by eye.

	const QString path = write_file ( QStringLiteral ( "bad.json" ), "{\n  \"a\": 1,\n}\n" );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Negative );

	const QStringList lines = lines_of ( outcome.output );

	QCOMPARE ( lines.size (), 1 );
	QVERIFY2 ( lines.front ().startsWith ( path + QStringLiteral ( ":3:1: error: " ) ), qPrintable ( lines.front () ) );

	// The message is the parser's own, so it is the one FILE-06's dialog shows for the same file.

	const LoadResult loaded = DocumentIo::load_text ( QStringLiteral ( "{\n  \"a\": 1,\n}\n" ) );

	QVERIFY  ( !loaded.ok );
	QCOMPARE ( lines.front (), path + QStringLiteral ( ":3:1: error: " ) + loaded.error.message );
}

void TestCliCommands::duplicate_keys_are_warnings_with_positions_and_do_not_fail ()
{
	const QString path = write_file ( QStringLiteral ( "duplicates.json" ), "{\n  \"id\": 1,\n  \"id\": 2,\n  \"say \\\"hi\\\"\": 3,\n  \"say \\\"hi\\\"\": 4\n}\n" );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Success );

	// The key is written as a JSON string literal, so one containing a quote stays unambiguous.

	QCOMPARE ( lines_of ( outcome.output ),
	           ( QStringList { path + QStringLiteral ( ": valid" ),
	                           path + QStringLiteral ( ":3:3: warning: duplicate key \"id\"" ),
	                           path + QStringLiteral ( ":5:3: warning: duplicate key \"say \\\"hi\\\"\"" ) } ) );
}

void TestCliCommands::a_file_that_cannot_be_read_is_exit_three ()
{
	const QString path = directory->filePath ( QStringLiteral ( "absent.json" ) );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code,   ExitCode::Failure );
	QCOMPARE ( outcome.output, path + QStringLiteral ( ": error: cannot be read: no such file\n" ) );
}

void TestCliCommands::a_directory_is_not_a_readable_file ()
{
	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), directory->path () } );

	QCOMPARE ( outcome.code, ExitCode::Failure );
	QVERIFY2 ( outcome.output.contains ( QStringLiteral ( "directory" ) ), qPrintable ( outcome.output ) );
}

void TestCliCommands::several_files_return_the_highest_code_data ()
{
	QTest::addColumn<QStringList> ( "kinds" );
	QTest::addColumn<int>         ( "expected" );

	// Every order, so the rule is "the highest" rather than "the last" or "the first".

	QTest::newRow ( "valid, valid" )             << QStringList { QStringLiteral ( "valid" ),    QStringLiteral ( "valid" ) }                               << 0;
	QTest::newRow ( "valid, invalid" )           << QStringList { QStringLiteral ( "valid" ),    QStringLiteral ( "invalid" ) }                             << 1;
	QTest::newRow ( "invalid, valid" )           << QStringList { QStringLiteral ( "invalid" ),  QStringLiteral ( "valid" ) }                               << 1;
	QTest::newRow ( "unreadable, invalid" )      << QStringList { QStringLiteral ( "absent" ),   QStringLiteral ( "invalid" ) }                             << 3;
	QTest::newRow ( "invalid, unreadable" )      << QStringList { QStringLiteral ( "invalid" ),  QStringLiteral ( "absent" ) }                              << 3;
	QTest::newRow ( "invalid, unreadable, ok" )  << QStringList { QStringLiteral ( "invalid" ),  QStringLiteral ( "absent" ), QStringLiteral ( "valid" ) } << 3;
}

void TestCliCommands::several_files_return_the_highest_code ()
{
	QFETCH ( QStringList, kinds );
	QFETCH ( int,         expected );

	QStringList arguments { QStringLiteral ( "--validate" ) };

	for ( int i = 0; i < kinds.size (); ++i )
	{
		const QString name = QStringLiteral ( "file-%1.json" ).arg ( i );

		if ( kinds [ i ] == QLatin1String ( "valid" ) )
		{
			arguments.append ( write_file ( name, "[]" ) );
		}
		else if ( kinds [ i ] == QLatin1String ( "invalid" ) )
		{
			arguments.append ( write_file ( name, "[" ) );
		}
		else
		{
			arguments.append ( directory->filePath ( name ) );
		}
	}

	const Outcome outcome = run ( arguments );

	QCOMPARE ( exit_status ( outcome.code ), expected );

	// One line per file, in the order given, whatever the outcome.

	QCOMPARE ( lines_of ( outcome.output ).size (), kinds.size () );
}

void TestCliCommands::a_byte_order_mark_is_tolerated ()
{
	// FILE-07: the window opens a file with a BOM, so the command line must call it valid.

	const QString path = write_file ( QStringLiteral ( "bom.json" ), QByteArray ( "\xEF\xBB\xBF" ) + "{ \"a\": 1 }" );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code,   ExitCode::Success );
	QCOMPARE ( outcome.output, path + QStringLiteral ( ": valid\n" ) );
}

void TestCliCommands::validate_reads_standard_input ()
{
	const Outcome good = run ( { QStringLiteral ( "--validate" ), QStringLiteral ( "-" ) }, "{ \"a\": 1 }" );

	QCOMPARE ( good.code,   ExitCode::Success );
	QCOMPARE ( good.output, QStringLiteral ( "<stdin>: valid\n" ) );

	const Outcome bad = run ( { QStringLiteral ( "--validate" ), QStringLiteral ( "-" ) }, "{ \"a\": }" );

	QCOMPARE ( bad.code, ExitCode::Negative );
	QVERIFY2 ( bad.output.startsWith ( QStringLiteral ( "<stdin>:1:8: error: " ) ), qPrintable ( bad.output ) );
}

void TestCliCommands::a_file_name_with_a_percent_sign_is_printed_as_named ()
{
	// QString::arg substitutes into what an earlier arg () inserted, so a chained report would print this name with
	// its "%2" replaced by the line number. The report is one multi-argument arg () for exactly this.

	const QString path = write_file ( QStringLiteral ( "odd%2name%1.json" ), "{" );

	const Outcome outcome = run ( { QStringLiteral ( "--validate" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Negative );
	QVERIFY2 ( outcome.output.startsWith ( path + QStringLiteral ( ":1:2: error: " ) ), qPrintable ( outcome.output ) );
}

//=====================================================================================================================
// --query
//=====================================================================================================================

void TestCliCommands::query_prints_pointers_in_document_order ()
{
	const QString path = write_file ( QStringLiteral ( "people.json" ),
	                                  "{ \"people\": [ { \"id\": 7, \"name\": \"Ada\" }, { \"id\": 9, \"name\": \"Grace\" } ], \"id\": 1 }" );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$..id" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Success );
	QVERIFY  ( outcome.error.isEmpty () );

	// Document order (QUERY-06): the two nested ids come before the top-level one because they come first in the file.

	QCOMPARE ( lines_of ( outcome.output ),
	           ( QStringList { QStringLiteral ( "/people/0/id" ), QStringLiteral ( "/people/1/id" ), QStringLiteral ( "/id" ) } ) );
}

void TestCliCommands::query_pointers_round_trip_through_go_to ()
{
	// Every line is a Go To input (QUERY-08): parsed as a pointer, it resolves in the document it came from -- keys
	// that need escaping included.

	const QByteArray json = "{ \"a/b\": { \"c~d\": 1 }, \"list\": [ { \"x\": 2 } ] }";
	const QString    path = write_file ( QStringLiteral ( "escapes.json" ), json );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$..*" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Success );

	const LoadResult loaded = DocumentIo::load_text ( QString::fromUtf8 ( json ) );

	QVERIFY ( loaded.ok );

	const QStringList lines = lines_of ( outcome.output );

	QVERIFY ( lines.contains ( QStringLiteral ( "/a~1b/c~0d" ) ) );

	for ( const QString& line : lines )
	{
		bool              parsed  = false;
		const JsonPointer pointer = JsonPointer::parse ( line, &parsed );

		QVERIFY2 ( parsed,                                                qPrintable ( line ) );
		QVERIFY2 ( pointer.resolve ( loaded.root.get () ) != nullptr,     qPrintable ( line ) );
	}
}

void TestCliCommands::query_prints_the_root_as_an_empty_line ()
{
	// "$" matches the root, whose pointer is the empty string (RFC 6901) -- an empty line, exactly as Copy JSONPath
	// Result copies it, rather than a label such as "(root)" that Go To would refuse.

	const QString path = write_file ( QStringLiteral ( "root.json" ), "[]" );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), path } );

	QCOMPARE ( outcome.code,   ExitCode::Success );
	QCOMPARE ( outcome.output, QStringLiteral ( "\n" ) );
}

void TestCliCommands::query_values_print_compact_json_with_number_tokens_as_written ()
{
	const QString path = write_file ( QStringLiteral ( "values.json" ),
	                                  "{ \"items\": [ { \"price\": 1.50, \"tags\": [ \"a\", \"b\" ] }, { \"price\": 1e3, \"note\": \"two\\nlines\" } ] }" );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$.items[*]" ), QStringLiteral ( "--values" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Success );

	// One line per result, whatever the value holds -- a line break inside a string stays escaped -- and the number
	// tokens exactly as written (FILE-10): 1.50 is not 1.5, and 1e3 is not 1000.

	QCOMPARE ( lines_of ( outcome.output ),
	           ( QStringList { QStringLiteral ( "{\"price\":1.50,\"tags\":[\"a\",\"b\"]}" ),
	                           QStringLiteral ( "{\"price\":1e3,\"note\":\"two\\nlines\"}" ) } ) );
}

void TestCliCommands::a_query_matching_nothing_is_exit_one_with_no_output ()
{
	const QString path = write_file ( QStringLiteral ( "empty-match.json" ), "{ \"a\": 1 }" );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$.missing" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Negative );
	QVERIFY  ( outcome.output.isEmpty () );
	QVERIFY  ( outcome.error.isEmpty () );
}

void TestCliCommands::a_malformed_query_names_its_column_and_is_exit_two ()
{
	// "$.a!b": the engine stops at the "!", the fourth character.

	const QString path = write_file ( QStringLiteral ( "any.json" ), "{ \"a\": 1 }" );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$.a!b" ), path } );

	QCOMPARE ( outcome.code, ExitCode::UsageError );
	QVERIFY  ( outcome.output.isEmpty () );

	const QStringList lines = lines_of ( outcome.error );

	QCOMPARE ( lines.size (), 3 );
	QVERIFY2 ( lines [ 0 ].startsWith ( QStringLiteral ( "vje: --query: column 4: " ) ), qPrintable ( outcome.error ) );
	QCOMPARE ( lines [ 1 ], QStringLiteral ( "  $.a!b" ) );
	QCOMPARE ( lines [ 2 ], QStringLiteral ( "     ^" ) );
}

void TestCliCommands::query_on_an_unreadable_file_is_exit_three ()
{
	const QString path = directory->filePath ( QStringLiteral ( "absent.json" ) );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Failure );
	QVERIFY  ( outcome.output.isEmpty () );
	QCOMPARE ( outcome.error, QStringLiteral ( "vje: " ) + path + QStringLiteral ( ": cannot be read: no such file\n" ) );
}

void TestCliCommands::query_on_invalid_json_is_exit_three ()
{
	// Exit 3 and not 1: "matched nothing" is an answer about a document, and there is no document.

	const QString path = write_file ( QStringLiteral ( "broken.json" ), "{ \"a\": " );

	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$" ), path } );

	QCOMPARE ( outcome.code, ExitCode::Failure );
	QVERIFY  ( outcome.output.isEmpty () );
	QVERIFY2 ( outcome.error.startsWith ( QStringLiteral ( "vje: " ) + path + QStringLiteral ( ":1:8: not valid JSON: " ) ), qPrintable ( outcome.error ) );
}

void TestCliCommands::query_reads_standard_input ()
{
	const Outcome outcome = run ( { QStringLiteral ( "--query" ), QStringLiteral ( "$.a" ), QStringLiteral ( "--values" ), QStringLiteral ( "-" ) },
	                              "{ \"a\": [ true, null ] }" );

	QCOMPARE ( outcome.code,   ExitCode::Success );
	QCOMPARE ( outcome.output, QStringLiteral ( "[true,null]\n" ) );
}

void TestCliCommands::a_command_reads_standard_input_only_when_asked ()
{
	// A script piping into `vje --validate file.json` must not have its input swallowed: standard input is read for
	// "-" and at no other time.

	const QString path = write_file ( QStringLiteral ( "named.json" ), "{}" );

	const CommandRegistry& registry = CommandRegistry::standard ();
	const LaunchPlan       plan     = plan_launch ( { QStringLiteral ( "vje" ), QStringLiteral ( "--validate" ), path }, registry );

	CapturingContext context;
	context.standardInput = "not json";

	QCOMPARE ( execute ( plan, registry, context ), ExitCode::Success );
	QCOMPARE ( context.standardInputReads, 0 );
}

//=====================================================================================================================
// The real context
//=====================================================================================================================

void TestCliCommands::text_the_platform_cannot_deliver_is_kept_for_the_message_box ()
{
	// The window program's message box shows whatever the WRITE could not deliver (main.cpp), so the claim worth
	// checking is that a refused write is reported and kept -- not dropped, and not reported as delivered. Standard
	// output is pointed somewhere that refuses writes for the length of one call and then put back: no handle at all on
	// Windows, which is what a window program launched from Explorer has, and /dev/full on Linux, which fails every
	// write the way a closed or full destination would.

	const QString text = QStringLiteral ( "VJE 2.0.0\n" );

	ConsoleCliContext context;

#if defined ( Q_OS_WIN )
	const HANDLE saved = GetStdHandle ( STD_OUTPUT_HANDLE );

	SetStdHandle ( STD_OUTPUT_HANDLE, nullptr );
	context.write_output ( text );
	SetStdHandle ( STD_OUTPUT_HANDLE, saved );
#else
	std::fflush ( stdout );

	const int saved = dup ( STDOUT_FILENO );
	const int full  = open ( "/dev/full", O_WRONLY );

	QVERIFY ( ( saved >= 0 ) && ( full >= 0 ) );

	dup2  ( full, STDOUT_FILENO );
	context.write_output ( text );
	std::clearerr ( stdout );
	dup2  ( saved, STDOUT_FILENO );
	close ( full );
	close ( saved );
#endif

	QCOMPARE ( context.undelivered_text (), text );
}

//---------------------------------------------------------------------------------------------------------------------
// The Explorer pair (CLI-08).
//---------------------------------------------------------------------------------------------------------------------

namespace
{
	// Run one of the pair directly -- constructed with its target, which the standard registry's copies cannot be --
	// through a capturing context.

	template <typename Command>
	ExplorerOutcome run_explorer_command ( const ExplorerCommandTarget& target )
	{
		const Command command ( target );

		CapturingContext context;

		ExplorerOutcome outcome;

		outcome.code   = command.run ( ParsedCommandLine (), CommandRegistry::standard (), context );
		outcome.output = context.output;
		outcome.error  = context.error;

		return outcome;
	}

	bool touch ( const QString& path )
	{
		QFile file ( path );

		return file.open ( QIODevice::WriteOnly );
	}
}

void TestCliCommands::register_writes_the_entries_and_records_yes ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "The Explorer pair exists on Windows only (CLI-08); tst_launch_plan asserts it is absent here." );
	}

	const ExplorerCommandTarget target = explorer_target ();

	QVERIFY ( touch ( target.windowProgramPath ) );

	const ExplorerOutcome outcome = run_explorer_command<RegisterExplorerIntegrationCommand> ( target );

	QCOMPARE ( outcome.code, ExitCode::Success );
	QVERIFY2 ( outcome.error.isEmpty (), qPrintable ( outcome.error ) );
	QVERIFY2 ( outcome.output.contains ( QDir::toNativeSeparators ( target.windowProgramPath ) ), qPrintable ( outcome.output ) );

	// The registry holds the whole plan for that program, and SET-15 says so -- the two in agreement.

	QVERIFY ( explorer::is_registered_for ( target.windowProgramPath, target.classesRoot ) );

	const SettingsStore settings ( target.settingsFilePath );

	QCOMPARE ( settings.value_bool ( settings_keys::EXPLORER_INTEGRATION, false ), true );
}

void TestCliCommands::unregister_removes_the_entries_and_records_no ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "The Explorer pair exists on Windows only (CLI-08); tst_launch_plan asserts it is absent here." );
	}

	const ExplorerCommandTarget target = explorer_target ();

	QVERIFY ( touch ( target.windowProgramPath ) );
	QCOMPARE ( run_explorer_command<RegisterExplorerIntegrationCommand> ( target ).code, ExitCode::Success );

	const ExplorerOutcome outcome = run_explorer_command<UnregisterExplorerIntegrationCommand> ( target );

	QCOMPARE ( outcome.code, ExitCode::Success );
	QVERIFY2 ( outcome.error.isEmpty (), qPrintable ( outcome.error ) );
	QCOMPARE ( outcome.output, QStringLiteral ( "Removed VJE's Windows Explorer entries.\n" ) );

	QVERIFY ( !explorer::is_registered ( target.classesRoot ) );

	const SettingsStore settings ( target.settingsFilePath );

	QVERIFY  ( settings.contains ( settings_keys::EXPLORER_INTEGRATION ) );
	QCOMPARE ( settings.value_bool ( settings_keys::EXPLORER_INTEGRATION, true ), false );
}

void TestCliCommands::unregister_with_nothing_registered_succeeds_and_writes_no_settings ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "The Explorer pair exists on Windows only (CLI-08); tst_launch_plan asserts it is absent here." );
	}

	// THE UNINSTALLER'S CASE: nothing registered, and no settings file. It must succeed -- an uninstaller cannot know
	// whether the user ever switched the feature on -- and it must not create a settings file on the way out, since an
	// absent setting already reads No.

	const ExplorerCommandTarget target = explorer_target ();

	const ExplorerOutcome outcome = run_explorer_command<UnregisterExplorerIntegrationCommand> ( target );

	QCOMPARE ( outcome.code, ExitCode::Success );
	QVERIFY2 ( outcome.error.isEmpty (), qPrintable ( outcome.error ) );
	QCOMPARE ( outcome.output, QStringLiteral ( "VJE has no Windows Explorer entries to remove.\n" ) );

	QVERIFY ( !QFileInfo::exists ( target.settingsFilePath ) );
}

void TestCliCommands::register_refuses_a_window_program_that_is_not_there ()
{
	if ( !explorer::is_supported () )
	{
		QSKIP ( "The Explorer pair exists on Windows only (CLI-08); tst_launch_plan asserts it is absent here." );
	}

	// Entries naming a missing program would put a menu item in Explorer that does nothing. Refused before anything is
	// written: exit 3, the program named, the registry and the settings file both untouched.

	const ExplorerCommandTarget target = explorer_target ();

	const ExplorerOutcome outcome = run_explorer_command<RegisterExplorerIntegrationCommand> ( target );

	QCOMPARE ( outcome.code, ExitCode::Failure );
	QVERIFY  ( outcome.output.isEmpty () );
	QVERIFY2 ( outcome.error.contains ( QDir::toNativeSeparators ( target.windowProgramPath ) ), qPrintable ( outcome.error ) );

	QVERIFY ( !explorer::is_registered ( target.classesRoot ) );
	QVERIFY ( !QFileInfo::exists ( target.settingsFilePath ) );
}

QTEST_GUILESS_MAIN ( TestCliCommands )

#include "tst_cli_commands.moc"

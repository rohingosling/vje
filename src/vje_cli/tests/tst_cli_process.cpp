//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for the BUILT executables (CLI-02, CLI-03, CLI-07, CLI-09) -- the half tst_cli_commands cannot reach,
//   because it is about what a process is handed and what it hands back: the command line Windows gives it, the bytes
//   it writes to a pipe, the code it exits with, and on Windows which subsystem each executable is linked for.
//
//   IT NEVER OPENS A WINDOW. Every case runs a command or a usage error, both of which are answered before any
//   application object that could draw; the one window launch -- the twin's hand-off -- is made to a stand-in
//   (handoff_probe.cpp) in a scratch directory. On Windows both executables are driven through PIPES, and a window
//   program whose streams are connected prints rather than showing a message box (main.cpp), so no case can meet a
//   modal it cannot dismiss. Every run still carries a timeout, so if that ever stops being true the case fails
//   instead of hanging the suite.
//
//   ON LINUX EVERY RUN IS MADE WITH NO DISPLAY -- DISPLAY, WAYLAND_DISPLAY and QT_QPA_PLATFORM all removed -- which is
//   CLI-03's "needs no display server" measured rather than assumed, for every command at once.
//
//   THE MESSAGE BOX IS NOT COVERED HERE and is left to the manual smoke: reaching it needs a window program launched
//   with no standard handles, and what it then shows is a modal nothing here could close.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace
{
	// How long any one run may take before it is treated as hung -- far beyond what a command needs, so a slow CI
	// machine never trips it, while a modal that should not have appeared still fails the case in bounded time.

	constexpr int PROCESS_TIMEOUT_MILLISECONDS = 60000;

	// A name no code page other than UTF-8 carries all of: Latin-1 accents, and two CJK characters that the Windows
	// ANSI code page 1252 turns into "??" (measured, 2026-09-25).

	const QString NON_ASCII_FILE_NAME = QStringLiteral ( "données-ñ-日本.json" );

	struct ProcessOutcome
	{
		bool    finished = false;
		int     exitCode = -1;
		QString output;
		QString error;
	};

	// The executable Explorer, a shortcut or a desktop launcher runs: vje.exe on Windows, the one vje on Linux.

	QString window_program ()
	{
		return QStringLiteral ( VJE_WINDOW_PROGRAM );
	}

	// The executable a terminal runs: vje.com on Windows, and on Linux the same file as the window program.

	QString console_program ()
	{
#if defined ( Q_OS_WIN )
		return QStringLiteral ( VJE_CONSOLE_PROGRAM );
#else
		return window_program ();
#endif
	}

	QProcessEnvironment environment_without_a_display ()
	{
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment ();

#if !defined ( Q_OS_WIN )
		environment.remove ( QStringLiteral ( "DISPLAY" ) );
		environment.remove ( QStringLiteral ( "WAYLAND_DISPLAY" ) );
		environment.remove ( QStringLiteral ( "QT_QPA_PLATFORM" ) );
#endif

		return environment;
	}

	ProcessOutcome run_program ( const QString& program, const QStringList& arguments, const QByteArray& standardInput = QByteArray (),
	                             const QProcessEnvironment& environment = environment_without_a_display () )
	{
		QProcess process;

		process.setProcessEnvironment ( environment );
		process.start ( program, arguments );

		ProcessOutcome outcome;

		if ( !process.waitForStarted ( PROCESS_TIMEOUT_MILLISECONDS ) )
		{
			outcome.error = QStringLiteral ( "could not start " ) + program + QStringLiteral ( ": " ) + process.errorString ();
			return outcome;
		}

		if ( !standardInput.isEmpty () )
		{
			process.write ( standardInput );
		}

		process.closeWriteChannel ();

		if ( !process.waitForFinished ( PROCESS_TIMEOUT_MILLISECONDS ) )
		{
			process.kill ();
			process.waitForFinished ();

			outcome.error = QStringLiteral ( "timed out: " ) + program + QLatin1Char ( ' ' ) + arguments.join ( QLatin1Char ( ' ' ) );
			return outcome;
		}

		outcome.finished = ( process.exitStatus () == QProcess::NormalExit );
		outcome.exitCode = process.exitCode ();

		// Decoded as UTF-8, which is the claim: a pipe gets UTF-8 bytes on both platforms (CLI-09).

		outcome.output = QString::fromUtf8 ( process.readAllStandardOutput () );
		outcome.error  = QString::fromUtf8 ( process.readAllStandardError () );

		return outcome;
	}

	QStringList executables_under_test ()
	{
		// Both Windows executables answer every command. On Linux they are one file, named once.

#if defined ( Q_OS_WIN )
		return { console_program (), window_program () };
#else
		return { console_program () };
#endif
	}

	QString executable_label ( const QString& program )
	{
		return QFileInfo ( program ).fileName ();
	}

#if defined ( Q_OS_WIN )

	// The Subsystem field of a PE image: 2 is a window program, 3 a console program. Read from the file, so "the window
	// program never shows a console" is checked against what the linker produced rather than against the CMake that
	// asked for it.

	constexpr quint16 SUBSYSTEM_WINDOWS_GUI = 2;
	constexpr quint16 SUBSYSTEM_WINDOWS_CUI = 3;

	int pe_subsystem ( const QString& path )
	{
		QFile file ( path );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			return -1;
		}

		const QByteArray image = file.read ( 4096 );

		auto read_u16 = [ & ] ( qsizetype offset ) -> quint32
		{
			return static_cast<quint8> ( image [ offset ] ) | ( static_cast<quint8> ( image [ offset + 1 ] ) << 8 );
		};

		auto read_u32 = [ & ] ( qsizetype offset ) -> quint32
		{
			return read_u16 ( offset ) | ( read_u16 ( offset + 2 ) << 16 );
		};

		if ( ( image.size () < 0x40 ) || !image.startsWith ( "MZ" ) )
		{
			return -1;
		}

		// e_lfanew -> "PE\0\0", then the 20-byte file header, then the optional header, whose Subsystem field is at
		// offset 68 in both the 32-bit and the 64-bit layouts.

		const qsizetype peHeader = static_cast<qsizetype> ( read_u32 ( 0x3C ) );

		if ( ( peHeader + 24 + 70 ) > image.size () || image.mid ( peHeader, 4 ) != QByteArray ( "PE\0\0", 4 ) )
		{
			return -1;
		}

		return static_cast<int> ( read_u16 ( peHeader + 24 + 68 ) );
	}

#endif
}

class TestCliProcess : public QObject
{
	Q_OBJECT

private slots:

	void init ();

	void both_executables_are_where_the_build_says ();
	void the_version_is_printed_by_every_executable ();
	void help_lists_the_commands ();
	void validate_reports_each_file_and_exits_with_the_highest_code ();
	void query_prints_pointers_and_values ();
	void query_reads_standard_input ();
	void a_malformed_query_exits_two_with_its_column ();
	void a_non_ascii_file_name_is_read_and_printed_intact ();
	void a_usage_error_exits_two_on_standard_error ();
	void commands_run_with_no_display ();

	// Windows only: the pair.

	void the_window_program_is_a_window_program_and_the_twin_a_console_program ();
	void the_twin_hands_a_window_launch_to_the_window_program ();
	void the_twin_without_its_window_program_is_exit_three ();

private:

	QString write_file ( const QString& name, const QByteArray& content );

	std::unique_ptr<QTemporaryDir> directory;
};

void TestCliProcess::init ()
{
	directory = std::make_unique<QTemporaryDir> ();

	QVERIFY ( directory->isValid () );
}

QString TestCliProcess::write_file ( const QString& name, const QByteArray& content )
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

void TestCliProcess::both_executables_are_where_the_build_says ()
{
	// Named "vje" (CLI-07), and on Windows side by side -- the twin finds its window program beside itself.

	for ( const QString& program : executables_under_test () )
	{
		QVERIFY2 ( QFileInfo::exists ( program ), qPrintable ( program ) );
		QCOMPARE ( QFileInfo ( program ).completeBaseName (), QStringLiteral ( "vje" ) );
	}

#if defined ( Q_OS_WIN )
	QCOMPARE ( QFileInfo ( console_program () ).absolutePath (), QFileInfo ( window_program () ).absolutePath () );
	QCOMPARE ( QFileInfo ( console_program () ).suffix (), QStringLiteral ( "com" ) );
	QCOMPARE ( QFileInfo ( window_program  () ).suffix (), QStringLiteral ( "exe" ) );
#endif
}

void TestCliProcess::the_version_is_printed_by_every_executable ()
{
	for ( const QString& program : executables_under_test () )
	{
		for ( const QString& spelling : { QStringLiteral ( "--version" ), QStringLiteral ( "-v" ) } )
		{
			const ProcessOutcome outcome = run_program ( program, { spelling } );

			QVERIFY2 ( outcome.finished, qPrintable ( executable_label ( program ) + QStringLiteral ( ": " ) + outcome.error ) );
			QCOMPARE ( outcome.exitCode, 0 );
			QCOMPARE ( outcome.output,   QStringLiteral ( "VJE 2.0.0\n" ) );
			QVERIFY  ( outcome.error.isEmpty () );
		}
	}
}

void TestCliProcess::help_lists_the_commands ()
{
	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome outcome = run_program ( program, { QStringLiteral ( "--help" ) } );

		QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
		QCOMPARE ( outcome.exitCode, 0 );

		for ( const QString& expected : { QStringLiteral ( "vje [<file>]" ), QStringLiteral ( "--validate" ), QStringLiteral ( "--query" ),
		                                   QStringLiteral ( "--values" ),    QStringLiteral ( "--help" ),     QStringLiteral ( "--version" ) } )
		{
			QVERIFY2 ( outcome.output.contains ( expected ), qPrintable ( executable_label ( program ) + QStringLiteral ( " lacks " ) + expected ) );
		}
	}
}

void TestCliProcess::validate_reports_each_file_and_exits_with_the_highest_code ()
{
	const QString good = write_file ( QStringLiteral ( "good.json" ), "{ \"a\": 1 }" );
	const QString bad  = write_file ( QStringLiteral ( "bad.json" ),  "{ \"a\": 1,\n}" );

	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome outcome = run_program ( program, { QStringLiteral ( "--validate" ), good, bad } );

		QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
		QCOMPARE ( outcome.exitCode, 1 );

		const QStringList lines = outcome.output.split ( QLatin1Char ( '\n' ), Qt::SkipEmptyParts );

		QCOMPARE ( lines.size (), 2 );
		QCOMPARE ( lines [ 0 ], good + QStringLiteral ( ": valid" ) );
		QVERIFY2 ( lines [ 1 ].startsWith ( bad + QStringLiteral ( ":2:1: error: " ) ), qPrintable ( lines [ 1 ] ) );

		// LF alone, on Windows too: the report is for scripts, and a CR would ride along into every line they read.

		QVERIFY ( !outcome.output.contains ( QLatin1Char ( '\r' ) ) );
	}
}

void TestCliProcess::query_prints_pointers_and_values ()
{
	const QString path = write_file ( QStringLiteral ( "items.json" ), "{ \"items\": [ { \"id\": 1.0 }, { \"id\": 2 } ] }" );

	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome pointers = run_program ( program, { QStringLiteral ( "--query" ), QStringLiteral ( "$..id" ), path } );

		QVERIFY2 ( pointers.finished, qPrintable ( pointers.error ) );
		QCOMPARE ( pointers.exitCode, 0 );
		QCOMPARE ( pointers.output,   QStringLiteral ( "/items/0/id\n/items/1/id\n" ) );

		const ProcessOutcome values = run_program ( program, { QStringLiteral ( "--query" ), QStringLiteral ( "$..id" ), QStringLiteral ( "--values" ), path } );

		QCOMPARE ( values.exitCode, 0 );
		QCOMPARE ( values.output,   QStringLiteral ( "1.0\n2\n" ) );

		const ProcessOutcome nothing = run_program ( program, { QStringLiteral ( "--query" ), QStringLiteral ( "$.absent" ), path } );

		QCOMPARE ( nothing.exitCode, 1 );
		QVERIFY  ( nothing.output.isEmpty () );
	}
}

void TestCliProcess::query_reads_standard_input ()
{
	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome outcome = run_program ( program, { QStringLiteral ( "--query" ), QStringLiteral ( "$[1]" ), QStringLiteral ( "--values" ), QStringLiteral ( "-" ) },
		                                             "[ \"a\", { \"b\": null } ]" );

		QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
		QCOMPARE ( outcome.exitCode, 0 );
		QCOMPARE ( outcome.output,   QStringLiteral ( "{\"b\":null}\n" ) );
	}
}

void TestCliProcess::a_malformed_query_exits_two_with_its_column ()
{
	const QString path = write_file ( QStringLiteral ( "any.json" ), "{}" );

	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome outcome = run_program ( program, { QStringLiteral ( "--query" ), QStringLiteral ( "$.a!b" ), path } );

		QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
		QCOMPARE ( outcome.exitCode, 2 );
		QVERIFY  ( outcome.output.isEmpty () );
		QVERIFY2 ( outcome.error.contains ( QStringLiteral ( "column 4" ) ), qPrintable ( outcome.error ) );
	}
}

void TestCliProcess::a_non_ascii_file_name_is_read_and_printed_intact ()
{
	// The CLI-02 / CLI-09 claim end to end: the name reaches the process intact (on Windows, from the wide command line
	// rather than the ANSI argv, which would have carried "??" for the CJK characters), the file opens under it, and
	// the report prints it as UTF-8. Through vje.exe as well as the twin: vje.exe's argv comes from Qt's WinMain, which
	// converts the wide command line to the ANSI code page, so it is the executable the old defect hit hardest.

	const QString path = write_file ( NON_ASCII_FILE_NAME, "[ 1 ]" );

	QVERIFY ( !path.isEmpty () );

	for ( const QString& program : executables_under_test () )
	{
		const ProcessOutcome outcome = run_program ( program, { QStringLiteral ( "--validate" ), path } );

		QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
		QCOMPARE ( outcome.exitCode, 0 );
		QCOMPARE ( outcome.output,   path + QStringLiteral ( ": valid\n" ) );
	}
}

void TestCliProcess::a_usage_error_exits_two_on_standard_error ()
{
	for ( const QString& program : executables_under_test () )
	{
		// An unknown option, and two files for the window -- the second is a window launch refused before any window
		// exists, which is why it is safe to run here at all.

		const QList<QStringList> lines { { QStringLiteral ( "--bogus" ) },
		                                 { QStringLiteral ( "a.json" ), QStringLiteral ( "b.json" ) } };

		for ( const QStringList& arguments : lines )
		{
			const ProcessOutcome outcome = run_program ( program, arguments );

			QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
			QCOMPARE ( outcome.exitCode, 2 );
			QVERIFY  ( outcome.output.isEmpty () );
			QVERIFY2 ( outcome.error.startsWith ( QStringLiteral ( "vje: " ) ),       qPrintable ( outcome.error ) );
			QVERIFY2 ( outcome.error.contains ( QStringLiteral ( "vje --help" ) ),    qPrintable ( outcome.error ) );
		}
	}
}

void TestCliProcess::commands_run_with_no_display ()
{
#if defined ( Q_OS_WIN )
	QSKIP ( "Windows always has a display; the no-display claim (CLI-03) is Linux's." );
#else
	// Every other case in this suite already runs with the display variables removed. This one says so by name, and
	// checks the removal actually happened, so the claim cannot quietly become untested.

	const QProcessEnvironment environment = environment_without_a_display ();

	QVERIFY ( !environment.contains ( QStringLiteral ( "DISPLAY" ) ) );
	QVERIFY ( !environment.contains ( QStringLiteral ( "WAYLAND_DISPLAY" ) ) );
	QVERIFY ( !environment.contains ( QStringLiteral ( "QT_QPA_PLATFORM" ) ) );

	const QString path = write_file ( QStringLiteral ( "headless.json" ), "{ \"a\": [ 1 ] }" );

	const ProcessOutcome validate = run_program ( console_program (), { QStringLiteral ( "--validate" ), path }, QByteArray (), environment );

	QVERIFY2 ( validate.finished, qPrintable ( validate.error ) );
	QCOMPARE ( validate.exitCode, 0 );

	const ProcessOutcome query = run_program ( console_program (), { QStringLiteral ( "--query" ), QStringLiteral ( "$.a[0]" ), path }, QByteArray (), environment );

	QCOMPARE ( query.exitCode, 0 );
	QCOMPARE ( query.output,   QStringLiteral ( "/a/0\n" ) );
#endif
}

void TestCliProcess::the_window_program_is_a_window_program_and_the_twin_a_console_program ()
{
#if !defined ( Q_OS_WIN )
	QSKIP ( "The window / console pair is Windows' (CLI-07); Linux has one executable." );
#else
	QCOMPARE ( pe_subsystem ( window_program () ),  static_cast<int> ( SUBSYSTEM_WINDOWS_GUI ) );
	QCOMPARE ( pe_subsystem ( console_program () ), static_cast<int> ( SUBSYSTEM_WINDOWS_CUI ) );
#endif
}

void TestCliProcess::the_twin_hands_a_window_launch_to_the_window_program ()
{
#if !defined ( Q_OS_WIN )
	QSKIP ( "The hand-off is the Windows twin's (CLI-07)." );
#else
	// A copy of the twin beside the stand-in, under the pair's names. The twin must pass the arguments through
	// UNCHANGED -- the toolkit's option and its value included, and the non-ASCII name intact -- and return at once.

	const QString twin   = directory->filePath ( QStringLiteral ( "vje.com" ) );
	const QString record = directory->filePath ( QStringLiteral ( "handoff.txt" ) );

	QVERIFY ( QFile::copy ( console_program (), twin ) );
	QVERIFY ( QFile::copy ( QStringLiteral ( VJE_HANDOFF_PROBE ), directory->filePath ( QStringLiteral ( "vje.exe" ) ) ) );

	QProcessEnvironment environment = environment_without_a_display ();
	environment.insert ( QStringLiteral ( "VJE_HANDOFF_RECORD" ), record );

	const QStringList arguments { QStringLiteral ( "-platform" ), QStringLiteral ( "offscreen" ), NON_ASCII_FILE_NAME };

	const ProcessOutcome outcome = run_program ( twin, arguments, QByteArray (), environment );

	QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
	QCOMPARE ( outcome.exitCode, 0 );
	QVERIFY  ( outcome.output.isEmpty () );
	QVERIFY  ( outcome.error.isEmpty () );

	// The child is detached, so the twin has returned before it necessarily ran; wait for its record.

	QTRY_VERIFY_WITH_TIMEOUT ( QFileInfo::exists ( record ), 15000 );

	QFile recorded ( record );

	QVERIFY ( recorded.open ( QIODevice::ReadOnly ) );

	QCOMPARE ( QString::fromUtf8 ( recorded.readAll () ), arguments.join ( QLatin1Char ( '\n' ) ) + QLatin1Char ( '\n' ) );
#endif
}

void TestCliProcess::the_twin_without_its_window_program_is_exit_three ()
{
#if !defined ( Q_OS_WIN )
	QSKIP ( "The hand-off is the Windows twin's (CLI-07)." );
#else
	const QString twin = directory->filePath ( QStringLiteral ( "vje.com" ) );

	QVERIFY ( QFile::copy ( console_program (), twin ) );

	const ProcessOutcome outcome = run_program ( twin, { QStringLiteral ( "sample.json" ) } );

	QVERIFY2 ( outcome.finished, qPrintable ( outcome.error ) );
	QCOMPARE ( outcome.exitCode, 3 );

	// The MESSAGE is what the twin's existence check buys, and so what this case must assert. Without the check,
	// startDetached on a missing file fails anyway and the exit code is still 3 -- a neutered build showed the code
	// alone could not tell them apart (lesson D25's shape: a guard bounding the wording rather than the outcome). A
	// missing partner is the one hand-off failure a user can fix, so it is named as such.

	QVERIFY2 ( outcome.error.contains ( QStringLiteral ( "vje.exe is not beside vje.com" ) ), qPrintable ( outcome.error ) );

	// A command still works: the twin needs its partner for the window and for nothing else.

	QCOMPARE ( run_program ( twin, { QStringLiteral ( "--version" ) } ).exitCode, 0 );
#endif
}

QTEST_GUILESS_MAIN ( TestCliProcess )

#include "tst_cli_process.moc"

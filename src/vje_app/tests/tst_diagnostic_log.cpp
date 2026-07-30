//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for DiagnosticLog (SET-09): off by default and writing nothing at all; on, writing one timestamped line per
//   call to the configured folder and file; following a settings change without a restart; and falling back to the
//   always-writable default folder -- announced exactly ONCE -- when the configured one cannot be written.
//
//   The default folder is injected, so the suite never writes into the real per-user data location.
//
//   Headless: the class is Qt Core only.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/DiagnosticLog.hpp"

#include "services/SettingsStore.hpp"

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

class TestDiagnosticLog : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void logging_is_off_by_default_and_writes_nothing ();
	void an_enabled_log_writes_timestamped_lines ();
	void enabling_it_needs_no_restart ();
	void an_unwritable_folder_falls_back_and_says_so_once ();

private:

	QString settings_path  () const;
	QString default_folder () const;
	QString configured_folder () const;

	QStringList read_lines ( const QString& path ) const;

	QTemporaryDir                  temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

QString TestDiagnosticLog::settings_path () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/settings.json" );
}

QString TestDiagnosticLog::default_folder () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/default-logs" );
}

QString TestDiagnosticLog::configured_folder () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/configured-logs" );
}

QStringList TestDiagnosticLog::read_lines ( const QString& path ) const
{
	QFile file ( path );

	if ( !file.open ( QIODevice::ReadOnly | QIODevice::Text ) )
	{
		return QStringList ();
	}

	return QString::fromUtf8 ( file.readAll () ).split ( QLatin1Char ( '\n' ), Qt::SkipEmptyParts );
}

void TestDiagnosticLog::init ()
{
	QVERIFY ( temporaryDirectory.isValid () );

	QFile::remove ( settings_path () );

	QDir ( default_folder () ).removeRecursively ();
	QDir ( configured_folder () ).removeRecursively ();

	settings = std::make_unique<SettingsStore> ( settings_path () );
}

void TestDiagnosticLog::cleanup ()
{
	settings.reset ();
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestDiagnosticLog::logging_is_off_by_default_and_writes_nothing ()
{
	DiagnosticLog log ( settings.get (), default_folder () );

	QVERIFY ( !log.is_enabled () );
	QVERIFY ( log.active_file_path ().isEmpty () );

	// Callers write unconditionally, so "off" has to mean nothing happens rather than a file with nothing in it.

	log.write ( QStringLiteral ( "should not appear" ) );

	QVERIFY ( !QDir ( default_folder () ).exists () );
}

void TestDiagnosticLog::an_enabled_log_writes_timestamped_lines ()
{
	settings->set_bool ( settings_keys::DIAGNOSTIC_LOGGING, true );
	settings->set_string ( settings_keys::LOG_FOLDER, configured_folder () );
	settings->set_string ( settings_keys::LOG_FILE_NAME, QStringLiteral ( "session.log" ) );

	DiagnosticLog log ( settings.get (), default_folder () );

	QVERIFY  ( log.is_enabled () );
	QCOMPARE ( log.active_file_path (), configured_folder () + QStringLiteral ( "/session.log" ) );

	log.write ( QStringLiteral ( "first" ) );
	log.write ( QStringLiteral ( "second" ) );

	const QStringList lines = read_lines ( log.active_file_path () );

	// Appended, not rewritten: a log that lost its earlier lines would be no use for the crash it exists to explain.

	QCOMPARE ( lines.size (), 2 );
	QVERIFY  ( lines [ 0 ].endsWith ( QStringLiteral ( "first" ) ) );
	QVERIFY  ( lines [ 1 ].endsWith ( QStringLiteral ( "second" ) ) );

	// Each line opens with a sortable timestamp -- "yyyy-MM-dd HH:mm:ss.zzz" -- so the entries order themselves.

	const QString stamp = lines [ 0 ].left ( 23 );

	QVERIFY ( QDateTime::fromString ( stamp, QStringLiteral ( "yyyy-MM-dd HH:mm:ss.zzz" ) ).isValid () );
}

void TestDiagnosticLog::enabling_it_needs_no_restart ()
{
	settings->set_string ( settings_keys::LOG_FOLDER, configured_folder () );

	DiagnosticLog log ( settings.get (), default_folder () );

	QVERIFY ( !log.is_enabled () );

	// The Settings dialog's OK is a write to the store, and this is all the class needs to hear about it (SET-09).

	settings->set_bool ( settings_keys::DIAGNOSTIC_LOGGING, true );

	QVERIFY ( log.is_enabled () );

	log.write ( QStringLiteral ( "after the toggle" ) );

	QCOMPARE ( read_lines ( log.active_file_path () ).size (), 1 );

	// And switching it back off stops it, without the file being touched again.

	settings->set_bool ( settings_keys::DIAGNOSTIC_LOGGING, false );

	QVERIFY ( !log.is_enabled () );

	log.write ( QStringLiteral ( "must not appear" ) );

	QCOMPARE ( read_lines ( configured_folder () + QLatin1Char ( '/' ) + settings_values::DEFAULT_LOG_FILE_NAME ).size (), 1 );
}

void TestDiagnosticLog::an_unwritable_folder_falls_back_and_says_so_once ()
{
	// A FILE where the folder should be: the folder cannot be created, and cannot be written -- the same state a
	// permission-denied folder puts the log in, reachable identically on both platforms.

	const QString blockedFolder = temporaryDirectory.path () + QStringLiteral ( "/blocked" );

	QFile blocker ( blockedFolder );

	QVERIFY ( blocker.open ( QIODevice::WriteOnly ) );

	blocker.close ();

	settings->set_string ( settings_keys::LOG_FOLDER, blockedFolder );

	DiagnosticLog log ( settings.get (), default_folder () );

	QSignalSpy fellBack ( &log, &DiagnosticLog::fell_back_to_default_folder );

	settings->set_bool ( settings_keys::DIAGNOSTIC_LOGGING, true );

	// SET-09: silently to the default folder, and one message box for the user -- not one per line.

	QCOMPARE ( fellBack.count (), 1 );
	QCOMPARE ( fellBack.first ().first ().toString (), blockedFolder );
	QCOMPARE ( log.active_file_path (), default_folder () + QLatin1Char ( '/' ) + settings_values::DEFAULT_LOG_FILE_NAME );

	log.write ( QStringLiteral ( "still logged" ) );
	log.write ( QStringLiteral ( "and again" ) );

	QCOMPARE ( read_lines ( log.active_file_path () ).size (), 2 );
	QCOMPARE ( fellBack.count (), 1 );

	// Naming a folder that works clears the state, so a later fault is announced again rather than swallowed.

	settings->set_string ( settings_keys::LOG_FOLDER, configured_folder () );

	QCOMPARE ( log.active_file_path (), configured_folder () + QLatin1Char ( '/' ) + settings_values::DEFAULT_LOG_FILE_NAME );

	settings->set_string ( settings_keys::LOG_FOLDER, blockedFolder );

	QCOMPARE ( fellBack.count (), 2 );
}

QTEST_MAIN ( TestDiagnosticLog )

#include "tst_diagnostic_log.moc"

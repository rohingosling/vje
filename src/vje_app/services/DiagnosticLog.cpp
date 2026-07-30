//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   DiagnosticLog implementation. See the header for the per-line flush and the fallback rule.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/DiagnosticLog.hpp"

#include "services/SettingsStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace vje
{
	namespace
	{
		// Sortable, sub-second, and unambiguous across a midnight boundary.

		const QString TIMESTAMP_FORMAT = QStringLiteral ( "yyyy-MM-dd HH:mm:ss.zzz" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	DiagnosticLog::DiagnosticLog ( SettingsStore* settings, const QString& defaultFolder, QObject* parent )
		: QObject         ( parent )
		, settings        ( settings )
		, fallbackFolder  ( defaultFolder.isEmpty () ? platform_default_folder () : defaultFolder )
	{
		if ( settings != nullptr )
		{
			connect ( settings, &SettingsStore::changed, this, &DiagnosticLog::handle_setting_changed );
		}

		resolve_target ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool DiagnosticLog::is_enabled () const
	{
		return loggingEnabled;
	}

	QString DiagnosticLog::active_file_path () const
	{
		if ( !loggingEnabled )
		{
			return QString ();
		}

		return activeFolder + QLatin1Char ( '/' ) + activeFileName;
	}

	QString DiagnosticLog::platform_default_folder ()
	{
		return QStandardPaths::writableLocation ( QStandardPaths::AppDataLocation ) + QStringLiteral ( "/logs" );
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	void DiagnosticLog::write ( const QString& message )
	{
		// Off is the default and the common case, so callers write unconditionally and this is where nothing happens.

		if ( !loggingEnabled )
		{
			return;
		}

		QFile file ( active_file_path () );

		if ( !file.open ( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text ) )
		{
			// The folder was writable when it was resolved. Losing the entry is the right failure here: a dialog per line
			// would be worse than the fault it is reporting.

			return;
		}

		const QString line = QDateTime::currentDateTime ().toString ( TIMESTAMP_FORMAT ) + QStringLiteral ( "  " ) + message + QLatin1Char ( '\n' );

		file.write ( line.toUtf8 () );

		// Flushed and closed per line, so the last thing written before a crash is on disk.

		file.flush ();
		file.close ();
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void DiagnosticLog::handle_setting_changed ( const QString& key )
	{
		if
		(
			( key == settings_keys::DIAGNOSTIC_LOGGING ) ||
			( key == settings_keys::LOG_FOLDER ) ||
			( key == settings_keys::LOG_FILE_NAME )
		)
		{
			resolve_target ();
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void DiagnosticLog::resolve_target ()
	{
		loggingEnabled = ( settings != nullptr ) && settings->value_bool ( settings_keys::DIAGNOSTIC_LOGGING, false );

		if ( !loggingEnabled )
		{
			// Nothing is resolved while logging is off, which is also why the folder and file-name editors are disabled
			// in the dialog (SET-09).

			activeFolder.clear ();
			activeFileName.clear ();
			announcedFallback = false;

			return;
		}

		const QString configuredFolder = settings->value_string ( settings_keys::LOG_FOLDER, QString () );
		const QString configuredName   = settings->value_string ( settings_keys::LOG_FILE_NAME, settings_values::DEFAULT_LOG_FILE_NAME );

		activeFileName = configuredName.isEmpty () ? settings_values::DEFAULT_LOG_FILE_NAME : configuredName;

		if ( !configuredFolder.isEmpty () && folder_is_writable ( configuredFolder ) )
		{
			activeFolder      = configuredFolder;
			announcedFallback = false;

			return;
		}

		activeFolder = fallbackFolder;

		folder_is_writable ( fallbackFolder );                     // Create it if this is the first run.

		// SET-09: the fallback is silent in the log itself and announced once to the user. A folder the setting never
		// named is not a fallback -- it is the default -- so only a configured one is worth a notice.

		if ( !configuredFolder.isEmpty () && !announcedFallback )
		{
			announcedFallback = true;

			emit fell_back_to_default_folder ( configuredFolder );
		}
	}

	bool DiagnosticLog::folder_is_writable ( const QString& folder )
	{
		QDir directory ( folder );

		if ( !directory.exists () && !directory.mkpath ( QStringLiteral ( "." ) ) )
		{
			return false;
		}

		// Existing is not the same as writable (a read-only or permission-denied folder exists perfectly well), so the
		// check is an actual write -- the only answer the platform cannot be wrong about.

		QFile probe ( directory.filePath ( QStringLiteral ( ".vje-write-probe" ) ) );

		if ( !probe.open ( QIODevice::WriteOnly | QIODevice::Truncate ) )
		{
			return false;
		}

		probe.close ();
		probe.remove ();

		return true;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   DiagnosticLog -- the opt-in diagnostic log of SET-09: off by default, gated on a setting, timestamped and flushed
//   per line so the last line survives a hard crash.
//
//   THREE DECISIONS WORTH STATING:
//
//     - Each line is its own open / append / flush / close. That is more system calls than a held-open handle, and it is
//       the point: a log that exists to explain a crash must not lose its final line to a buffer. The cost is irrelevant
//       because the log is off unless someone is diagnosing something.
//     - A configured folder that cannot be written falls back to the always-writable default and says so ONCE (SET-09).
//       Silently dropping the entries would make the log lie by omission, and a message box per line would be worse than
//       the fault.
//     - The default folder is a constructor argument. The application passes the platform's per-user data location; a
//       test passes a temp folder, so the suite never writes into the real one.
//
//   The class re-reads its settings on every change, so enabling logging takes effect the moment the Settings dialog's
//   OK commits -- no restart.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QObject>
#include <QString>

namespace vje
{
	class SettingsStore;

	//*****************************************************************************************************************
	// Class: DiagnosticLog
	//*****************************************************************************************************************

	class DiagnosticLog : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// defaultFolder is the fallback used when the setting names none, or names one that cannot be written. Empty
		// means platform_default_folder().

		explicit DiagnosticLog ( SettingsStore* settings, const QString& defaultFolder = QString (), QObject* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool is_enabled () const;

		// The file entries are being written to, or an empty string while logging is off.

		QString active_file_path () const;

		// QStandardPaths::AppDataLocation + "/logs" -- always writable, and per-user on both platforms.

		static QString platform_default_folder ();

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Append one timestamped line. A no-op while logging is off, so callers need not ask first.

		void write ( const QString& message );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The configured folder could not be written, so the default is being used instead. Emitted once per fallback,
		// not once per line; the application answers it with a message box (SET-09).

		void fell_back_to_default_folder ( const QString& configuredFolder );

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_setting_changed ( const QString& key );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void resolve_target ();                                    // Re-read the settings and pick the folder / file.

		// Can this folder be created and written? The check that decides the SET-09 fallback.

		static bool folder_is_writable ( const QString& folder );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		SettingsStore* settings;                                   // Non-owning.

		QString fallbackFolder;                                    // The always-writable default.
		QString activeFolder;                                      // Where entries are actually going.
		QString activeFileName;
		bool    loggingEnabled = false;

		// One notice per fallback: raised when the resolve falls back, cleared when it no longer needs to.

		bool announcedFallback = false;
	};
}

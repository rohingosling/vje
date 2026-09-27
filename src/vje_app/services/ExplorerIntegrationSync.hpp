//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ExplorerIntegrationSync -- keeps Windows Explorer's menus in step with SET-15 while the window is running
//   (FILE-15). It does two things and decides very little:
//
//     when the setting changes    Yes registers this window program, No removes VJE's entries. The Settings dialog
//                                 writes the setting on OK like any other, and the change reaches this class through
//                                 the store's signal -- so the dialog knows nothing about Explorer.
//
//     at launch (refresh)         while the setting is Yes, the entries are rewritten if they no longer name THIS
//                                 window program -- a portable copy moved, or an update installed elsewhere -- so they
//                                 never point at a VJE that is not there. While it is No, nothing is touched.
//
//   A FAILURE LEAVES THE SETTING TELLING THE TRUTH. If the registry cannot be written, the setting is put back to what
//   the registry actually holds and failed () carries the reason, which the window shows. The Explorer pair on the
//   command line keeps the same agreement from the other side (CLI-08).
//
//   ROOT-SCOPED, CONSTRUCTED IN main.cpp AND NOWHERE ELSE, and that is a safety property rather than a style. Every
//   window harness in the suite (tst_main_window, tst_settings_dialog) builds a window around a temporary settings
//   file, and an OK there writes every field the store does not yet hold -- SET-15's No included. Were this class
//   window-scoped, that write would remove the developer's real Explorer entries in the middle of a test run.
//   Outside main.cpp nothing constructs it, and its own suite hands it a scratch registry root.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <platform/explorer_integration.hpp>

#include <QObject>
#include <QString>

namespace vje
{
	class SettingsStore;

	//*****************************************************************************************************************
	// Class: ExplorerIntegrationSync
	//*****************************************************************************************************************

	class ExplorerIntegrationSync : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// `windowProgramPath` is what the entries launch -- vje_cli::window_program_path () in the application.
		// `classesRoot` is the live root except under test.

		ExplorerIntegrationSync
		(
			SettingsStore* settings,
			const QString& windowProgramPath,
			const QString& classesRoot = platform::explorer_integration::LIVE_CLASSES_ROOT,
			QObject*       parent      = nullptr
		);

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// The launch-time check: while SET-15 is Yes, make sure the entries name this window program. Does nothing
		// while it is No, and nothing on a platform without the feature.

		void refresh ();

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The registry could not be brought into line with the setting. The setting has already been corrected to what
		// the registry holds; `problem` is one sentence saying why.

		void failed ( const QString& problem );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void setting_changed ( const QString& key );
		void apply           ( bool enabled );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		SettingsStore* settings;
		QString        windowProgram;
		QString        classesRoot;

		// Set while this class writes the setting back after a failure, so that write is not taken for the user's.

		bool correcting = false;
	};
}

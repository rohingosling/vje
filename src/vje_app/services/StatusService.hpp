//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   StatusService -- the sink for transient status messages (VAL-04). Any layer posts a
//   message here (save/load results, validation errors, find-match position, background progress, update notices) and
//   the status bar's message area shows it, decoupling producers from the widget. A timeout of 0 means the message
//   persists until replaced or cleared.
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
	//*****************************************************************************************************************
	// Class: StatusService
	//*****************************************************************************************************************

	class StatusService : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit StatusService ( QObject* parent = nullptr );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void show_message ( const QString& text, int timeoutMilliseconds = 0 );   // 0 => until replaced/cleared.
		void clear        ();

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void message_posted  ( const QString& text, int timeoutMilliseconds );
		void message_cleared ();
	};
}

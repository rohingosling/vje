//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   StatusService implementation -- a pass-through signal hub; it holds no state, so the status bar is the single
//   source of truth for what is currently displayed.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "StatusService.hpp"

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	StatusService::StatusService ( QObject* parent )
		: QObject ( parent )
	{
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void StatusService::show_message ( const QString& text, int timeoutMilliseconds )
	{
		emit message_posted ( text, timeoutMilliseconds );
	}

	void StatusService::clear ()
	{
		emit message_cleared ();
	}
}

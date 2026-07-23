//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   SelectionService implementation -- a thin observable holder around the current JsonPointer selection and its
//   origin. It never resolves the pointer against a document; consumers do that, so the service has no document
//   dependency and stays trivially correct.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "SelectionService.hpp"

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// Reveal-intent rule: every explicit navigation reveals the node; a form-field
	// write-back and the programmatic no-reveal case do not disturb the tree's expansion.
	//-----------------------------------------------------------------------------------------------------------------

	bool reveals_selection ( SelectionOrigin origin )
	{
		switch ( origin )
		{
			case SelectionOrigin::Tree:
			case SelectionOrigin::GoTo:
			case SelectionOrigin::Find:
			case SelectionOrigin::Paste:
			case SelectionOrigin::DrillIn:
				return true;

			case SelectionOrigin::FormField:
			case SelectionOrigin::Programmatic:
				return false;
		}

		return false;
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	SelectionService::SelectionService ( QObject* parent )
		: QObject ( parent )
	{
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool SelectionService::has_selection () const
	{
		return hasSelection;
	}

	const JsonPointer& SelectionService::selection () const
	{
		return currentSelection;
	}

	SelectionOrigin SelectionService::origin () const
	{
		return currentOrigin;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void SelectionService::set_selection ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		currentSelection = pointer;
		currentOrigin    = origin;
		hasSelection     = true;

		// Always re-emit: re-selecting the same pointer from a different gesture still carries a distinct reveal
		// intent that observers must act on (e.g. a Find match re-revealing an already-selected node).

		emit selection_changed ( currentSelection, currentOrigin );
	}

	void SelectionService::clear ()
	{
		if ( !hasSelection )
		{
			return;
		}

		hasSelection     = false;
		currentSelection = JsonPointer ();
		currentOrigin    = SelectionOrigin::Programmatic;

		emit selection_cleared ();
	}
}

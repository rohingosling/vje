//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
			case SelectionOrigin::CodeCaret:
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

	const QList<JsonPointer>& SelectionService::selection_set () const
	{
		return currentSelectionSet;
	}

	int SelectionService::selection_count () const
	{
		return int ( currentSelectionSet.size () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void SelectionService::set_selection ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		set_multiple_selection ( { pointer }, pointer, origin );
	}

	void SelectionService::set_multiple_selection
	(
		const QList<JsonPointer>& pointers,
		const JsonPointer&        primary,
		SelectionOrigin           origin
	)
	{
		if ( pointers.isEmpty () )
		{
			clear ();

			return;
		}

		// The primary must be IN the set (TREE-04). Taking the first rather than appending the stray one keeps the
		// caller's set exactly as given -- it is the caller's statement of what is selected, and quietly growing it
		// would make a command act on a node the tree never highlighted.

		const JsonPointer resolvedPrimary = pointers.contains ( primary ) ? primary : pointers.first ();

		const bool setChanged = ( currentSelectionSet != pointers );

		currentSelection    = resolvedPrimary;
		currentSelectionSet = pointers;
		currentOrigin       = origin;
		hasSelection        = true;

		// Always re-emit the primary: re-selecting the same pointer from a different gesture still carries a distinct
		// reveal intent that observers must act on (e.g. a Find match re-revealing an already-selected node). The set
		// is emitted only when it actually differs, because it carries no intent for a re-emission to deliver.

		emit selection_changed ( currentSelection, currentOrigin );

		if ( setChanged )
		{
			emit selection_set_changed ( currentSelectionSet );
		}
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

		currentSelectionSet.clear ();

		// The set is emitted alongside the clear rather than left implicit: a consumer of the set alone (a multi-node
		// command's enablement) would otherwise have to know that selection_cleared empties it too.

		emit selection_set_changed ( currentSelectionSet );
		emit selection_cleared ();
	}
}

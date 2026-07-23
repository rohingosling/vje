//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   SelectionService -- the single holder of "the selected node", named by JsonPointer (NAV-01). The tree writes it; the editor pane and status bar observe it. Each change
//   carries a SelectionOrigin so
//   observers can honour the REVEAL INTENT: a form-field-originated selection must NOT expand the tree, while Go To /
//   Find / paste / drill-in DO expand to reveal (EDITOR-04) -- the clean, event-based form of a 1.0 lesson.
//
//   Phase 5 stands the service up and the status bar observes it (node-path / node-info panes); the tree that writes
//   it and the reveal-honouring consumer arrive in Phase 6.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QObject>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// Where a selection change originated -- the input to the reveal-intent rule.
	//-----------------------------------------------------------------------------------------------------------------

	enum class SelectionOrigin
	{
		Programmatic,       // Set by the app (e.g. document load selects root); no expand-to-reveal.
		Tree,               // The user clicked / keyed the tree.
		FormField,          // Object-form field focus write-back; selects WITHOUT expanding a collapsed branch.
		GoTo,               // Edit > Go To (FIND-04); expands to reveal.
		Find,               // A find match (FIND-02); expands to reveal.
		Paste,              // A paste target; expands to reveal.
		DrillIn             // Form drill-in into a {...} / [...] (EDITOR-05); expands to reveal.
	};

	// Whether a selection from this origin should expand the tree to reveal the node (EDITOR-04). FormField and the
	// programmatic no-op case leave a collapsed branch collapsed; every explicit navigation reveals.

	bool reveals_selection ( SelectionOrigin origin );

	//*****************************************************************************************************************
	// Class: SelectionService
	//*****************************************************************************************************************

	class SelectionService : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit SelectionService ( QObject* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool               has_selection () const;
		const JsonPointer& selection     () const;   // Meaningful only while has_selection() is true.
		SelectionOrigin    origin        () const;   // The origin of the current selection.

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_selection ( const JsonPointer& pointer, SelectionOrigin origin );
		void clear         ();                       // No node selected (e.g. document closed).

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void selection_changed ( const JsonPointer& pointer, SelectionOrigin origin );
		void selection_cleared ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonPointer     currentSelection;
		SelectionOrigin currentOrigin = SelectionOrigin::Programmatic;
		bool            hasSelection  = false;
	};
}

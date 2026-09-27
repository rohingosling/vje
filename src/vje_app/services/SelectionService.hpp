//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
//   THE PRIMARY AND THE SET (TREE-04 / TREE-09, Phase 15e). Multiple tree selection did NOT turn selection() into a
//   set. Eleven collaborators read it -- the editor pane, the status bar, FindController, PrintController, the command
//   enablement, both context menus -- and every one of them asks "which node is the subject", which is a question a
//   set cannot answer. So the primary stays exactly what it was and the SET arrives beside it, read only by the code
//   that acts on several nodes at once. The set always contains the primary, and a single-node write collapses it to
//   that node, which is what keeps every existing caller's meaning unchanged.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QList>
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
		DrillIn,            // Form drill-in into a {...} / [...] (EDITOR-05); expands to reveal.
		CodeCaret           // A double-click in the Code View named a node (EDITOR-07); expands to reveal.
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
		const JsonPointer& selection     () const;   // The PRIMARY. Meaningful only while has_selection() is true.
		SelectionOrigin    origin        () const;   // The origin of the current selection.

		// The whole selection, in document order, always containing the primary (TREE-09). It is the primary alone
		// unless a multiple selection has been written, so code that acts on one node has no reason to look here and
		// code that acts on several has exactly one place to look.

		const QList<JsonPointer>& selection_set   () const;
		int                       selection_count () const;   // 0 while has_selection() is false.

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// The single-node write, unchanged for every existing caller -- and it COLLAPSES the set to this one node.
		// That is not a convenience: a primary that moved while a stale set stayed behind would leave the multi-node
		// commands acting on nodes the user has navigated away from.

		void set_selection ( const JsonPointer& pointer, SelectionOrigin origin );

		// The multiple-node write (TREE-09). The primary must be one of the pointers; if it is not, the first is taken
		// instead rather than leaving the two disagreeing. An empty list clears.

		void set_multiple_selection
		(
			const QList<JsonPointer>& pointers,
			const JsonPointer&        primary,
			SelectionOrigin           origin
		);

		void clear ();                               // No node selected (e.g. document closed).

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void selection_changed ( const JsonPointer& pointer, SelectionOrigin origin );

		// The set changed. Unlike selection_changed -- which re-emits unconditionally because its origin carries a
		// distinct reveal intent every time -- this fires only when the set's CONTENTS differ, since a set carries no
		// intent and a re-emission of the same one says nothing.
		//
		// It is for a consumer that cares about the SET ALONE. Anything that already observes selection_changed needs
		// no second connection, because every write of the set writes a primary and re-emits that too -- and a primary
		// can move while the set does not (Shift+click within a range the user has already built), so the two are
		// genuinely different questions rather than one of them being finer than the other.
		//
		// IT HAS NO CONSUMER IN THE APPLICATION, and after Phase 15f that is a finding rather than a deferral. 15e
		// expected EDIT-14's multi-node commands to be the consumer; they are not, because the set is siblings under
		// one parent and the primary is one of them, so every precondition those commands have is answered identically
		// by the primary. A count changes what a command DOES, not whether it is available.

		void selection_set_changed ( const QList<JsonPointer>& pointers );

		void selection_cleared ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonPointer        currentSelection;
		QList<JsonPointer> currentSelectionSet;
		SelectionOrigin    currentOrigin = SelectionOrigin::Programmatic;
		bool               hasSelection  = false;
	};
}

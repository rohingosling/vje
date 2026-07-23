//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   IEditorView / IEditorViewProvider -- the pluggable editor view contract (EDITOR-01).
//
//   THE POINT OF THE SEAM. A new view must be addable without touching an existing one (LD-6). A provider exposes a
//   stable identifier, a display order, an icon, a can_present() predicate, and a factory; EditorPane queries the
//   registered providers, filters by can_present, and builds one tab per applicable view in registered order. Version
//   2.0 registers Form (Phase 7), Text, and Code (Phase 8); a fourth is one provider class plus one registration line.
//
//   WHY A PROVIDER AND NOT JUST A VIEW. can_present() has to be answerable BEFORE a view exists -- that is what decides
//   whether the tab is there at all -- and the display order has to be stable across a tab-set rebuild for EDITOR-10's
//   "the active tab persists across selection changes" to mean anything. Both are properties of the view KIND, not of
//   an instance.
//
//   A view implementation inherits QWidget first and this second (Q_OBJECT requires the QObject base to come first);
//   widget() then returns this.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "services/SelectionService.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QString>
#include <QWidget>

namespace vje
{
	class JsonNode;

	//*****************************************************************************************************************
	// Class: IEditorView
	//*****************************************************************************************************************

	class IEditorView
	{
		//=============================================================================================================
		// Constructors / Destructor
		//=============================================================================================================

	public:

		virtual ~IEditorView () = default;

		//=============================================================================================================
		// Contract
		//=============================================================================================================

	public:

		// The widget hosted in the tab. An implementation deriving from QWidget returns this.

		virtual QWidget* widget () = 0;

		// Show the node the pointer names. What that means is the view's own business -- the Form View, for instance,
		// answers a SCALAR pointer by presenting its PARENT with the corresponding field current (EDITOR-02).
		//
		// PRESENTING IS PASSIVE. It moves what is shown and what is current; it never takes keyboard focus and never
		// opens an editor. The user may be arrowing down the tree, and a view that grabs the caret because the selection
		// changed takes the keyboard away mid-navigation (EDITOR-04's "passive tree navigation never disturbs...").
		// Handing over the caret is a GESTURE, and gestures arrive below.
		//
		// The origin is carried through because it still changes behaviour rather than merely explaining it: the Code
		// View scrolls to a Tree-originated selection without moving its caret, while a form-field write-back must not
		// disturb the view it came from.

		virtual void present ( const JsonPointer& pointer, SelectionOrigin origin ) = 0;

		// The tab became the visible one. A view that defers work until it is on screen does it here.

		virtual void view_activated () {}

		// The tab is about to stop being the visible one. Returning false ABORTS the switch, which is how the Code
		// View will hold an invalid uncommitted edit (EDITOR-09, Phase 8). Nothing in Phase 7 refuses.

		virtual bool view_deactivating () { return true; }

		// The tree's two hand-over GESTURES (EDITOR-04). Both are deliberate user acts on a tree row, as distinct from
		// the selection changing -- which also happens when the user merely arrows past a row, and must not hand over
		// anything.
		//
		//   tree_node_clicked  -- a single click on a row. Whether that is an activation gesture is the view's own
		//                         "Edit on" setting to decide (SET-05 / SET-07); the default is that it is not.
		//   activate_editing   -- the unconditional activation gesture: Enter, or a double-click.
		//
		// Both are no-ops for a read-only view.

		virtual void tree_node_clicked () {}

		virtual void activate_editing () {}

		// Give the view the keyboard, without editing anything. Tab hands focus here on its way round the workspace
		// (NAV-04), and landing in a view must not be an activation gesture -- the same rule present() follows.
		//
		// The default is enough for a single-widget view. A view with several faces overrides it to focus whichever one
		// is on screen, and to make sure there is something current to land on.

		virtual void take_focus () { widget ()->setFocus ( Qt::TabFocusReason ); }

		// Does this view want the Tab key for ITSELF? Answering true takes the view out of the NAV-04 cycle while it
		// has focus, so Tab reaches the view's own handler instead.
		//
		// Nothing in Phase 7 does. This exists for the Code View (Phase 8), where Tab and Shift+Tab manage indentation
		// rather than moving focus (EDITOR-07) -- the one place the workspace-wide meaning of Tab has to yield.

		virtual bool claims_tab_key () const { return false; }
	};

	//*****************************************************************************************************************
	// Class: IEditorViewProvider
	//*****************************************************************************************************************

	class IEditorViewProvider
	{
		//=============================================================================================================
		// Constructors / Destructor
		//=============================================================================================================

	public:

		virtual ~IEditorViewProvider () = default;

		//=============================================================================================================
		// Contract
		//=============================================================================================================

	public:

		// A stable identifier, used to restore the active tab across a tab-set rebuild (EDITOR-10). Not user-visible
		// and never translated.

		virtual QString view_id () const = 0;

		virtual QString display_name () const = 0;   // The tab's label.

		// An IconLibrary name (STYLE-06). May be empty, and may name a glyph the library does not yet hold -- the tab
		// then shows its label alone rather than a blank square.

		virtual QString icon_name () const = 0;

		// Position in the tab strip. Lower first; ties keep registration order.

		virtual int display_order () const = 0;

		// Whether this view is applicable to a node, and therefore whether its tab appears at all (EDITOR-01). A null
		// node means an empty document.

		virtual bool can_present ( const JsonNode* node ) const = 0;

		// Build the view, bound to whatever collaborators the provider was constructed with. Ownership passes to the
		// caller (in practice to the tab widget, by Qt parenting).

		virtual IEditorView* create_view ( QWidget* parent ) const = 0;
	};
}

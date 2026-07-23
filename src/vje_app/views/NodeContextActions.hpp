//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   NodeContextActions -- the node command set the context menu presents, and the one builder that lays it out.
//
//   The actions are MainWindow's SHARED QActions, so the menu bar, the toolbar, and every context menu are enabled and
//   disabled together -- one source of truth, disabled-not-hidden. Every member is optional: a null
//   action is simply left out, which is what lets a pane be constructed bare in tests.
//
//   Two panes now offer this menu: the tree, on a node (TREE-06), and the Form View, on a key label (EDITOR-02, "the
//   same node context menu as the tree -- the same commands acting on that key's node"). It lives here rather than in
//   either of them so the two cannot drift into offering different commands for the same concept.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

class QAction;
class QMenu;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The shared command actions a node context menu presents.
	//-----------------------------------------------------------------------------------------------------------------

	struct NodeContextActions
	{
		QAction* cut       = nullptr;
		QAction* copy      = nullptr;
		QAction* paste     = nullptr;

		QAction* duplicate = nullptr;
		QAction* remove    = nullptr;
		QAction* rename    = nullptr;

		QAction* moveUp    = nullptr;
		QAction* moveDown  = nullptr;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Fill a menu with the node commands in their canonical order and grouping. The menu is NOT executed and no
	// trailing separator is added, so a caller is free to append its own pane-specific commands (the tree appends
	// Expand / Collapse Subtree).
	//
	// The Add Child / Add Sibling submenus are added present-and-DISABLED. They are the EDIT-04 pair -- the same six
	// types as the Document menu's flat Add commands, but relative to the selected node in two different ways -- so
	// they need their own actions rather than borrowing the existing ones, which is Phase 9's work.
	//-----------------------------------------------------------------------------------------------------------------

	void populate_node_context_menu ( QMenu* menu, const NodeContextActions& actions );
}

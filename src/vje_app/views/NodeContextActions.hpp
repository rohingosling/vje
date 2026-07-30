//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include <vje_core/document/JsonNode.hpp>

#include <functional>

class QAction;
class QMenu;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The shared command actions a node context menu presents.
	//
	// The flat commands are MainWindow's shared QActions (one enablement source, disabled-not-hidden). The EDIT-04
	// Add Child / Add Sibling submenus are different: they are one command per JSON type in two placements, so instead
	// of sixteen persistent actions they are callbacks MainWindow supplies, invoked with the chosen kind, and gated by
	// two predicates the menu evaluates AS IT OPENS -- so the enablement is always current against the live selection
	// without any per-selection upkeep.
	//-----------------------------------------------------------------------------------------------------------------

	struct NodeContextActions
	{
		QAction* cut       = nullptr;
		QAction* copy      = nullptr;
		QAction* paste     = nullptr;

		// Copy JSON Pointer (FIND-05) -- a clipboard command, so it sits in the clipboard group, and AFTER Paste rather
		// than beside Copy: Cut / Copy / Paste is a triad users read as one unit, and splitting it to keep the two
		// "copy" verbs adjacent costs more than it buys.

		QAction* copyPointer = nullptr;

		QAction* duplicate = nullptr;
		QAction* remove    = nullptr;
		QAction* rename    = nullptr;

		QAction* moveUp    = nullptr;
		QAction* moveDown  = nullptr;

		// The three array transforms (EDIT-11..13), below the move pair. Shared QActions like the rest, so the
		// centralized enablement -- object-only, array-only, and array-of-objects-only respectively -- travels with them
		// into every menu that offers them.

		QAction* objectsToArray = nullptr;
		QAction* arrayToObjects = nullptr;
		QAction* normalizeArray = nullptr;

		std::function<void ( JsonKind )> addChild;        // EDIT-04: add as the selection's last child.
		std::function<void ( JsonKind )> addSibling;      // EDIT-04: add as the sibling after the selection.
		std::function<bool ()>           canAddChild;     // Evaluated when the menu opens (a container selection).
		std::function<bool ()>           canAddSibling;   // Evaluated when the menu opens (a non-root selection).
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Fill a menu with the node commands in their canonical order and grouping. The menu is NOT executed and no
	// trailing separator is added, so a caller is free to append its own pane-specific commands (the tree appends the
	// four expand / collapse commands, which are its alone).
	//
	// The Add Child / Add Sibling submenus are the EDIT-04 pair -- the same six types as the Document menu's flat Add
	// commands, but relative to the selected node in two placements -- built from the callbacks and gating predicates
	// the struct carries, evaluated as the menu opens.
	//-----------------------------------------------------------------------------------------------------------------

	void populate_node_context_menu ( QMenu* menu, const NodeContextActions& actions );

	//-----------------------------------------------------------------------------------------------------------------
	// Fill a menu with the six typed add commands, in the Document menu's order, each item invoking callback with its
	// kind. The ONE statement of that set: the context submenus above and MainWindow's Insert / Ctrl+Insert choosers
	// all populate through here, so a seventh kind cannot reach one surface and miss another (EDIT-03 / 04).
	//-----------------------------------------------------------------------------------------------------------------

	void populate_add_type_menu ( QMenu* menu, const std::function<void ( JsonKind )>& callback );
}

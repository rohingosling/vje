//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   What an edit command SAYS about how it went (VAL-04), as one pure rule over ( command, outcome ).
//
//   THE HOLE THIS FILLS WAS A FAMILY, NOT A COMMAND. Until Phase 15 the Document menu's commands each decided for
//   themselves: Rename Key and Paste raised a QMessageBox on a refusal, Cut and Copy posted to the status bar on
//   success, and Add / Duplicate / Delete / Move / Normalize / both Converts said nothing at all in any outcome. The
//   silent Unchanged was the worst of them -- normalizing an array that is already uniform does nothing, and a
//   command that does nothing and says nothing is indistinguishable from one that is broken.
//
//   So the rule is stated ONCE, here, and the wording is DATA. A per-command message is unavoidable -- EditOutcome
//   carries no reason, and "Only an array of objects can be normalized" cannot be derived from Rejected -- but which
//   CHANNELS an outcome reaches is the part that must not vary, and that is what this function fixes:
//
//     Applied    -- the status bar, naming the command and the node it acted on. Not a modal: a second surface would
//                   make a completed command read as a failure (the rule Phase 12.5 set for a successful export).
//     Unchanged  -- the status bar, saying WHY nothing happened. Never a modal; a no-op is not an error, and the user
//                   is mid-flow.
//     Rejected   -- the status bar AND a modal, because it is the one outcome the user cannot see for themselves.
//                   Rare in practice, since the centralized enablement (MainWindow::update_command_enablement) already
//                   guards most refusals before a command runs.
//
//   THE RULE IS TOTAL. Every ( command, outcome ) pair announces something, including pairs no current code path can
//   produce -- tst_edit_reporting asserts exactly that. A table with a hole in it is how a command comes to be silent
//   again later, quietly, because someone made a previously unreachable outcome reachable.
//
//   Pure, so the whole policy is pinned in the HEADLESS harness with no widget and no message box to hide behind.
//   MainWindow::report_edit is the only thing that turns an announcement into a status message and a dialog.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>

#include <QString>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The edit commands that report through this rule -- one per UndoController call the command surface makes.
	//
	// Cut and Copy are deliberately absent. They are CLIPBOARD commands whose message is about what reached the
	// clipboard rather than about an EditOutcome, and cutting the root is a copy that is not a failed cut -- a
	// statement this rule has no way to make. Their own wording stays where it is, in MainWindow.
	//-----------------------------------------------------------------------------------------------------------------

	enum class EditCommand
	{
		AddChild,
		AddSibling,
		RenameKey,
		Duplicate,
		Delete,
		MoveUp,
		MoveDown,
		NormalizeArray,
		ArrayToObjects,
		ObjectsToArray,
		PasteNode,

		// Convert To (EDIT-09), ONE ENUMERATOR PER TARGET TYPE rather than one command carrying a JsonKind.
		//
		// The wording is data here, and what the user needs to be told differs per target -- "Converted /a/b to a
		// number" and "It is already a number." both name the type. Passing the kind alongside would make this the one
		// command whose message is assembled rather than looked up, and would put a second parameter on a signature
		// every other command answers without.

		ChangeTypeToString,
		ChangeTypeToNumber,
		ChangeTypeToBoolean,
		ChangeTypeToNull,

		// EDIT-10's drag-to-reorder, which is its own command rather than MoveUp / MoveDown with a longer reach.
		//
		// The reason is the Unchanged wording, which is what reporting a reorder is FOR. Move Up and Move Down are
		// no-ops only at an edge, so their wording says so -- "Already the first item." A drag's no-op is a different
		// fact: the run landed where it already was, which happens anywhere in a container and says nothing about
		// edges. Reusing those two strings here would state a plausible-but-wrong cause, which the wording table's own
		// rule forbids more strongly than it forbids silence.

		MoveNodes,

		// EDIT-15's array sort. Its %1 is the ARRAY -- the sort's subject is the container, not the elements that
		// moved inside it, and counting those would answer a question nobody asked.
		//
		// The message deliberately does not name the column or the direction. Both are on screen at the moment it is
		// posted: the user has just clicked that column's marker, and the marker now shows which way it points. This
		// is Phase 9's rule for the Move commands' enablement -- say what the user cannot see, not what they can.

		SortArray,

		// EDITOR-18's column delete, which is the one operation in the table with no single-node equivalent: it
		// removes a member from EVERY element of the array. Its %1 is the ARRAY, and the column is deliberately not
		// named -- the user has just clicked that column's header, and it is gone.
		//
		// A row delete needs no enumerator: a row IS an element, so it reports as EditCommand::Delete with the
		// element's pointer, which is exactly what deleting it from the tree already says.

		DeleteColumn,

		// EDITOR-19's column clear, DeleteColumn's neighbour in the same menu and deliberately its own command
		// rather than a Delete that writes null. Its %1 is the ARRAY for DeleteColumn's reason -- the user has just
		// aimed at that column's header -- and its Unchanged wording is the one that earns the enumerator, since a
		// column already full of nulls is the reachable no-op here and "no element carries that member" would be a
		// plausible-but-wrong cause for it.

		ClearColumn,

		// EDITOR-22's row clear, ClearColumn turned through a right angle. Its %1 is the ELEMENT rather than the array,
		// because a row IS an element -- naming it is both true and short, as Delete's %1 is for a row delete -- and
		// its Unchanged wording earns the enumerator for ClearColumn's reason: a row already full of nulls is the
		// reachable no-op, and ClearColumn's "That column is already empty." would name the wrong thing.

		ClearRow,

		// EDITOR-21's column rename, which is a member rename applied across every element of the array. Its own
		// command rather than RenameKey with a wider reach, for the reason DeleteColumn is its own: the wording has to
		// name what was touched, and "Renamed /users/0/email" is untrue of an edit that renamed it on forty elements.
		//
		// Its Rejected wording carries the two refusals the enablement cannot pre-empt -- a single-value array has no
		// member to rename, and a name already in use would make members unreachable unless SET-03a permits it.

		RenameColumn
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The Convert To command for a target kind, so the menu, the handler and the reporting agree on the pairing without
	// each carrying its own four-way mapping. Kinds the menu does not offer (Object, Array -- EDIT-09) have no command
	// and are a programming error to ask for.
	//-----------------------------------------------------------------------------------------------------------------

	EditCommand change_type_command ( JsonKind targetKind );

	//-----------------------------------------------------------------------------------------------------------------
	// What to say, and where. An empty modalTitle means the status bar alone.
	//-----------------------------------------------------------------------------------------------------------------

	struct EditAnnouncement
	{
		QString message;      // Status bar text. Never empty -- the rule is total.
		QString modalTitle;   // Non-empty => ALSO raise a modal carrying `message` under this title.

		bool is_modal () const;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The name a node goes by in a user-facing message.
	//
	// One spelling for the whole application: the RFC 6901 text, except at the root, whose pointer is the empty string
	// and would otherwise render as "Deleted " with nothing after it. FindController's Go To reaches the same problem
	// and now asks here, so the two cannot drift into describing the same node two ways.
	//-----------------------------------------------------------------------------------------------------------------

	QString node_display_text ( const JsonPointer& target );

	//-----------------------------------------------------------------------------------------------------------------
	// What the command's SUBJECT goes by, for the commands EDIT-14 gave a set to act on. One node is named; several are
	// counted, because listing four pointers in a status bar is not something anybody reads.
	//
	// The singular is node_display_text's, so a one-node multi-node command reports exactly what the single-node one
	// always did -- which is what makes EDIT-14 invisible until it is used.
	//-----------------------------------------------------------------------------------------------------------------

	QString subject_display_text ( const JsonPointer& target, int subjectCount );

	//-----------------------------------------------------------------------------------------------------------------
	// THE RULE. Total over ( command, outcome ).
	//
	// subjectCount is how many nodes the command acted on, and it is meaningful only for the commands whose %1 is the
	// SUBJECT -- Delete and MoveNodes (EDIT-14, EDIT-10). Paste's %1 is its DESTINATION, which is one node however
	// many arrived there, so the multi-node paste calls this with the default like every single-node command does.
	//-----------------------------------------------------------------------------------------------------------------

	EditAnnouncement announce_edit
	(
		EditCommand        command,
		EditOutcome        outcome,
		const JsonPointer& target,
		int                subjectCount = 1
	);
}

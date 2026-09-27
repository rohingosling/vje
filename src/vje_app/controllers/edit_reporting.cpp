//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   edit_reporting implementation. See the header for why the rule is one function and why it is total.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/edit_reporting.hpp"

#include <QCoreApplication>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// One command's wording. The three outcome strings are stated per command because EditOutcome carries no
		// reason -- Rejected cannot say WHICH precondition failed, so the caller's own knowledge of the command is the
		// only place the reason exists.
		//
		// EVERY REASON HERE IS THE ONE UndoController ACTUALLY APPLIES, read off its implementation rather than
		// guessed. A message that states a plausible-but-wrong cause is worse than the silence it replaced: it sends
		// the user to fix something that was never the problem.
		//-------------------------------------------------------------------------------------------------------------

		struct CommandWording
		{
			const char* title;       // Modal title on a refusal, and the command's own name.
			const char* applied;     // %1 => the target's display text.
			const char* unchanged;
			const char* rejected;
		};

		CommandWording wording_for ( EditCommand command )
		{
			switch ( command )
			{
				case EditCommand::AddChild:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Add Child" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Added %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nothing was added." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "A child can only be added to an object or an array." )
					};

				case EditCommand::AddSibling:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Add Sibling" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Added %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nothing was added." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The document root has no sibling." )
					};

				case EditCommand::RenameKey:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Rename Key" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Renamed %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The key is unchanged." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That key already exists in this object." )
					};

				case EditCommand::Duplicate:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Duplicate" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Duplicated %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nothing was duplicated." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The document root cannot be duplicated." )
					};

				case EditCommand::Delete:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Delete" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Deleted %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nothing was deleted." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The document root cannot be deleted." )
					};

				// EDIT-08's two directions are separate commands rather than one carrying a MoveDirection, because
				// their Unchanged wording is the whole point of reporting them: "already at the top" and "already at
				// the bottom" are different facts, and a user who cannot tell them apart cannot tell either from a
				// dead keyboard shortcut.
				//
				// NEITHER Unchanged IS REACHABLE FROM THE MENU, THE TOOLBAR OR THE CONTEXT MENU, and that is correct:
				// update_command_enablement disables Move Up at index 0 and Move Down at the last sibling, which
				// Phase 9 chose deliberately ("Move is disabled at the edges rather than a silent no-op"). A greyed
				// command is BETTER feedback than an enabled one that refuses, and unlike Phase 12.5's export case
				// the reason is plain on screen -- the user can see it is the first row. Confirmed by manual test,
				// 2026-08-02.
				//
				// The wording still is not speculative -- UndoController::move_node genuinely returns Unchanged at a
				// boundary, pinned in tst_undo_controller -- but it stays unreachable from the user interface, and
				// Phase 15f is where that stopped being a deferral and became a decision.
				//
				// 15f predicted the drag would reach these two strings, and it does not. A drag's no-op is that the
				// run landed where it already was, which happens anywhere in a container and says nothing about
				// edges; answering it with "Already the first item." would state a plausible-but-wrong cause, which
				// the note at the head of this table forbids more strongly than it forbids silence. EDIT-10's no-op
				// is EditCommand::MoveNodes below, with wording that is true wherever the drop landed.

				case EditCommand::MoveUp:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Move Up" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Moved %1 up" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Already the first item." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The document root cannot be moved." )
					};

				case EditCommand::MoveDown:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Move Down" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Moved %1 down" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Already the last item." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The document root cannot be moved." )
					};

				// The motivating case. normalize_array_elements is idempotent, so a uniform array rebuilds an equal
				// subtree -- which UndoController::replace_subtree now reports as Unchanged rather than pushing a
				// no-op undo step (Phase 15). Before that this command dirtied the document and said nothing.

				case EditCommand::NormalizeArray:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Normalize Array Elements" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Normalized %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Every element already carries the same members." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only an array whose elements are all objects can be normalized." )
					};

				case EditCommand::ArrayToObjects:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert Array to Objects" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to objects" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The array is already in that form." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only an array can be converted to objects." )
					};

				case EditCommand::ObjectsToArray:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert Objects to Array" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to an array" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The object is already in that form." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only an object can be converted to an array." )
					};

				// EDIT-10's drag-to-reorder. Applied names the subject -- one node's pointer, or a count once EDIT-14
				// gave commands a set to act on -- because after a drag the interesting fact is what moved, not where
				// it came from.
				//
				// Unchanged is the reason this command exists (see MoveUp above): a drop can land where the selection
				// already was, and that is not an edge condition, so it is worded without reference to one. It is the
				// only reorder no-op the application can reach, because whether one exists depends on the drop TARGET
				// and no enablement can pre-empt it.
				//
				// Rejected states the SAME-PARENT rule rather than a resolve failure, because that is the rule
				// TreeViewPane::apply_drop and JsonTreeModel::canDropMimeData both enforce -- and by the time a drop
				// gets here, a cross-parent one has already been refused twice, so anything left is a selection that
				// stopped naming what it named when the drag began.

				case EditCommand::MoveNodes:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Move" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Moved %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nothing moved: the drop landed where the selection already was." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Nodes can only be reordered within their own parent." )
					};

				// EDIT-15. Unchanged is genuinely reachable and is the most ordinary thing in this table: clicking a
				// column's marker twice in the same direction, or sorting a column whose values are all equal.
				//
				// Rejected is not reachable through the header, which only ever offers a column the table is actually
				// projecting -- so the wording states the rule (an array) rather than describing a lookup failure the
				// user has no way to have caused.

				case EditCommand::SortArray:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Sort" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Sorted %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The array is already in that order." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only an array can be sorted." )
					};

				// EDITOR-18. Unchanged is reachable and worth distinguishing from Applied: a column can be shown for
				// a member that only SOME elements carry, and deleting it from an array where none of them do is a
				// no-op rather than a failure.
				//
				// Rejected states the rule the enablement cannot pre-empt: a single-value (scalar) array has no
				// member to remove, and emptying it is a delete of every ROW wearing this command's name.

				case EditCommand::DeleteColumn:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Delete Column" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Deleted a column from %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "No element carries that member." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "This array's elements have no members to remove." )
					};

				// EDITOR-19. Rejected states what the enablement cannot pre-empt: the column names nothing in the
				// projection. Unlike Delete Column there is no scalar-array refusal -- clearing a single-value
				// table's one column is meaningful, which is exactly the divergence clear_column's own comment
				// records.

				case EditCommand::ClearColumn:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Clear Contents" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Cleared a column of %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That column is already empty." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That column cannot be cleared." )
					};

				// EDITOR-22. Rejected is reached only by a row that is not an element; the provisional row, which is the
				// one a user can actually select, is refused by the controller in its own words before it gets here.
				//
				// The title names the ROW although the menu item reads "Clear Contents" in both menus: a modal's title
				// says which command refused (no_two_commands_share_a_refusal_message), and ClearColumn already has
				// the bare name.

				case EditCommand::ClearRow:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Clear Row Contents" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Cleared the contents of %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That row is already empty." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That row cannot be cleared." )
					};

				case EditCommand::RenameColumn:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Rename Column" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Renamed a column of %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The column already has that name." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "That column cannot be renamed to that name." )
					};

				case EditCommand::PasteNode:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Paste" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Pasted into %1" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The clipboard content is already there." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "The clipboard content cannot be pasted here." )
					};

				// EDIT-09. The Unchanged wording carries the whole reason this family is offered on its OWN current
				// type: masking it would make the submenu's contents change with the selection, so the four commands
				// are always present and the one that would do nothing says so.
				//
				// The Rejected wording names the SCALAR rule rather than the resolve failure, because the enablement
				// already refuses a container -- so the only way here is a selection that stopped resolving, and
				// "cannot be converted" with the rule beside it is true of both.

				case EditCommand::ChangeTypeToString:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert to String" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to a string" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "It is already a string." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only a scalar value can be converted to a string." )
					};

				case EditCommand::ChangeTypeToNumber:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert to Number" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to a number" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "It is already a number." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only a scalar value can be converted to a number." )
					};

				case EditCommand::ChangeTypeToBoolean:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert to Boolean" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to a boolean" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "It is already a boolean." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only a scalar value can be converted to a boolean." )
					};

				case EditCommand::ChangeTypeToNull:
					return
					{
						QT_TRANSLATE_NOOP ( "edit_reporting", "Convert to Null" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Converted %1 to null" ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "It is already null." ),
						QT_TRANSLATE_NOOP ( "edit_reporting", "Only a scalar value can be converted to null." )
					};
			}

			// Unreachable for a value of the enumeration, and deliberately not a default label inside the switch --
			// omitting one is what makes a newly added command a COMPILE error (-Wswitch) rather than a silent
			// fallthrough to generic wording.

			return { "", "", "", "" };
		}
	}

	//=================================================================================================================
	// EditAnnouncement
	//=================================================================================================================

	bool EditAnnouncement::is_modal () const
	{
		return !modalTitle.isEmpty ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QString node_display_text ( const JsonPointer& target )
	{
		return target.is_root ()
		     ? QCoreApplication::translate ( "edit_reporting", "the document root" )
		     : target.to_string ();
	}

	EditCommand change_type_command ( JsonKind targetKind )
	{
		switch ( targetKind )
		{
			case JsonKind::String:  return EditCommand::ChangeTypeToString;
			case JsonKind::Number:  return EditCommand::ChangeTypeToNumber;
			case JsonKind::Boolean: return EditCommand::ChangeTypeToBoolean;
			case JsonKind::Null:    return EditCommand::ChangeTypeToNull;

			// EDIT-09 defines a conversion to both containers and the menu deliberately does not offer either, so
			// nothing can ask for one. Answering with a scalar command would report a conversion that did not happen.

			case JsonKind::Object:
			case JsonKind::Array:
				break;
		}

		Q_ASSERT_X ( false, "change_type_command", "Convert To is offered for scalar targets only (EDIT-09)" );

		return EditCommand::ChangeTypeToNull;
	}

	QString subject_display_text ( const JsonPointer& target, int subjectCount )
	{
		// The plural form only, and never reached with a count of one -- so there is no singular "%n nodes" spelling
		// to get wrong. An untranslated tr ( "%n node(s)" ) renders the literal "(s)" (lesson Q42), which is exactly
		// what a phrase built to serve both counts would leave on screen in every build this project ships.

		if ( subjectCount > 1 )
		{
			return QCoreApplication::translate ( "edit_reporting", "%n nodes", nullptr, subjectCount );
		}

		return node_display_text ( target );
	}

	EditAnnouncement announce_edit ( EditCommand command, EditOutcome outcome, const JsonPointer& target, int subjectCount )
	{
		const CommandWording wording = wording_for ( command );

		switch ( outcome )
		{
			case EditOutcome::Applied:
			{
				return
				{
					QCoreApplication::translate ( "edit_reporting", wording.applied )
						.arg ( subject_display_text ( target, subjectCount ) ),
					QString ()
				};
			}

			case EditOutcome::Unchanged:
			{
				return { QCoreApplication::translate ( "edit_reporting", wording.unchanged ), QString () };
			}

			case EditOutcome::Rejected:
			{
				return
				{
					QCoreApplication::translate ( "edit_reporting", wording.rejected ),
					QCoreApplication::translate ( "edit_reporting", wording.title )
				};
			}
		}

		return {};
	}
}

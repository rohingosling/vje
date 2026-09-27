//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   UndoController -- the clean vje_core-facing surface over a QUndoStack. It owns the
//   stack, translates each product edit into the right EditCommand after checking the edit's preconditions, and
//   binds the document's dirty flag to the stack's clean state so undoing back to the saved point clears the modified
//   indicator (UNDO-04).
//
//   Every edit operation returns an EditOutcome: Applied (a command was pushed), Rejected (a precondition failed, e.g.
//   a duplicate key VAL-02, or an invalid number VAL-03 -- nothing changed), or Unchanged (the edit was a no-op, e.g.
//   a rename to the same key or a value set to its current value -- nothing pushed). Targets are named by JsonPointer,
//   the same way selection and Go To name nodes.
//
//   Preconditions enforced here (not in the commands, which assume valid input): duplicate-key rejection on rename
//   and object-member add (VAL-02); JSON-number validation on a number set (VAL-03); array/object kind gates on the
//   transforms (EDIT-11..13); and refusal to delete or duplicate the root.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/Validator.hpp>
#include <vje_core/editing/paste_target_plan.hpp>

#include <QList>
#include <QObject>
#include <QString>
#include <QUndoStack>

#include <optional>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The result of an edit operation.
	//-----------------------------------------------------------------------------------------------------------------

	enum class EditOutcome
	{
		Applied,        // A command was pushed onto the stack; the document changed.
		Rejected,       // A precondition failed (VAL-02 / VAL-03 / kind gate / root guard); nothing changed.
		Unchanged       // The edit was a no-op (same value / same key / boundary move); nothing pushed.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Direction for a reorder (EDIT-08).
	//-----------------------------------------------------------------------------------------------------------------

	enum class MoveDirection
	{
		Up,
		Down
	};

	//*****************************************************************************************************************
	// Class: UndoController
	//*****************************************************************************************************************

	class UndoController : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit UndoController ( JsonDocument* document, QObject* parent = nullptr );

		//=============================================================================================================
		// VAL-02 / SET-03a -- the duplicate-key policy
		//=============================================================================================================

	public:

		// May an edit create a second member with a key its object already carries? Default NO, which is VAL-02 as it
		// has always stood; Yes defers to a user who has said they want the duplicates RFC 8259 permits, and both of
		// this class's guards then stand down.
		//
		// PUSHED IN rather than read, because vje_core must not acquire a settings store -- the rule ThemeService and
		// Card already follow for the settings they answer to, arrived at there from the opposite direction (a reader
		// in vje_core would drag it into every test target that compiles this one).
		//
		// It governs the REFUSAL and never the NAMING. EDIT-07's `(copy)` sequence and EDIT-16's column naming avoid a
		// collision by choosing a different key, which is a rule about what to CALL a thing rather than about whether
		// to allow it, and neither consults this.

		void set_allow_duplicate_keys ( bool allow );
		bool allow_duplicate_keys     () const;

		//=============================================================================================================
		// Stack Interface
		//=============================================================================================================

	public:

		QUndoStack* stack () const;                                // For wiring undo/redo QActions in a later phase.

		bool can_undo () const;
		bool can_redo () const;
		void undo     ();
		void redo     ();

		bool is_clean () const;                                    // True when the stack is at the last-marked-clean state.
		void set_clean ();                                         // Mark the current state as the saved baseline (UNDO-04).
		void clear     ();                                         // Drop all history and mark clean (e.g. after a load).

		// UNDO-05. True while undo () or redo () is applying a step -- including while the step's batched change
		// notifications go out, which is when a view hears about the rows the step brought back. An observer that
		// wants to treat a replay's changes differently from an ordinary edit's asks this from inside its own handler,
		// and hears replayed () once everything has arrived.

		bool is_replaying () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// An undo () or redo () has finished and every change it made has been announced. Emitted whether or not the
		// stack had a step to replay; an observer that recorded nothing during is_replaying () has nothing to do.

		void replayed ();

	public:

		//=============================================================================================================
		// Scalar Value Edits (EDIT-01)
		//=============================================================================================================

	public:

		EditOutcome set_string  ( const JsonPointer& target, const QString& text );
		EditOutcome set_number  ( const JsonPointer& target, const QString& token );   // token validated as a JSON number.
		EditOutcome set_boolean ( const JsonPointer& target, bool value );

		//=============================================================================================================
		// Structural Edits
		//=============================================================================================================

	public:

		EditOutcome rename_key ( const JsonPointer& target, const QString& newKey );    // EDIT-02.

		// EDIT-03 add with automatic placement: a container selection receives the new node as its last child; a
		// scalar selection receives it as the sibling immediately after itself. key is used only for an object parent.

		EditOutcome add_node ( const JsonPointer& selection, JsonKind kind, const QString& key = QString () );

		// EDIT-04 explicit placement: add as the last child of a container / as the sibling after a node.

		EditOutcome add_child   ( const JsonPointer& container, JsonKind kind, const QString& key = QString () );
		EditOutcome add_sibling ( const JsonPointer& target,    JsonKind kind, const QString& key = QString () );

		EditOutcome delete_node    ( const JsonPointer& target );                       // EDIT-05.

		// EDIT-14: delete SEVERAL nodes as one undo step. Returns how many were removed.
		//
		// THE ORDER IS THE POINT, and it is why this is here rather than a loop at the call site. An array element's
		// pointer token IS its position, so removing [1] renames [2] to [1] -- and a caller walking its selection
		// forward would delete the wrong node from the second one onward. Going in DESCENDING sibling order means
		// every removal only renumbers siblings that have already been dealt with. (An object member's pointer is its
		// key and survives either way; the rule is stated once for both rather than made to depend on the kind.)
		//
		// PRECONDITION: the targets are siblings, which is what TREE-09 confines a multiple selection to. That is what
		// makes "descending sibling order" well defined -- across two parents there is no single order to sort into.
		// Anything that does not resolve, or is the root, is skipped rather than failing the group.

		int delete_nodes ( const QList<JsonPointer>& targets );
		EditOutcome duplicate_node ( const JsonPointer& target );                       // EDIT-07.
		EditOutcome move_node      ( const JsonPointer& target, MoveDirection direction );  // EDIT-08.

		// EDIT-10's reorder primitive: move one child of the named container from one position to another, where
		// toIndex is the position it ends up at once it has been lifted out (JsonNode::move_child's convention).
		//
		// IT NAMES THE PARENT AND TWO INDICES RATHER THAN THE NODE, and that is what makes a multi-node reorder
		// expressible at all. A drag applies a SEQUENCE of these, and every one of them renumbers the siblings that
		// follow it -- so a plan phrased as pointers would be stale after its own first step, which is the positional-
		// pointer trap (Q22) arriving one layer up. The parent's pointer, by contrast, is fixed for the whole run.
		//
		// Rejected when the pointer does not resolve to a container or either index is out of range; Unchanged when
		// the two indices are equal, which is the drop that landed where the node already was (EDIT-10).

		EditOutcome move_child ( const JsonPointer& parentPointer, int fromIndex, int toIndex );

		// EDIT-15: reorder an array by one of the array table's columns, as ONE undo step.
		//
		// memberKey names the column: std::nullopt sorts by the ELEMENTS themselves (the single-column scalar array),
		// a value sorts by that member of each object element, and an element without it sorts as null. The order is
		// json_order's and is stated in full there and in spec EDIT-15.
		//
		// Rejected when the pointer does not resolve to an array; Unchanged when the array holds fewer than two
		// elements or is already in the requested order, which is a real and reachable no-op -- clicking the same
		// header twice in the same direction, or sorting a column whose values are all equal.

		EditOutcome sort_array
		(
			const JsonPointer&            arrayPointer,
			const std::optional<QString>& memberKey,
			Qt::SortOrder                 order
		);

		//=============================================================================================================
		// EDIT-16 -- pasting a copied COLUMN (a named list of values) at a node, as ONE undo step.
		//=============================================================================================================

	public:

		// One value of a pasted column. A null node is an ABSENT cell (a ragged array's missing member): EDIT-16
		// writes it as `null` wherever the paste must CREATE a slot for it -- a list has no absent slot -- and leaves
		// an EXISTING target cell untouched.

		struct PastedValue
		{
			std::unique_ptr<JsonNode> value;
		};

		// EDIT-16. Where the values land is plan_paste_target's decision; this applies it.
		//
		// listName is the name the column travels with (column_naming's rule at the copy end). route says which
		// selection aimed the paste -- a tree selection names an ARRAY and asks what it should become, a column
		// selection names a COLUMN and asks what it should hold -- and targetColumnName is the column the header route
		// aimed at, absent on a single-column table which has a name but no member key.
		//
		// Rejected when the target does not resolve, or is a shape EDIT-16 refuses; the refusal text is the plan's.
		// Unchanged when the values are all absent, which creates nothing.
		//
		// PasteRoute::InsertColumn places the new column IN FRONT OF targetColumnName on every element (in front of
		// the reshaped values' own column, where a single-column array is converted), rather than last. pastedName,
		// where given, receives the name the values were written under -- the plan's, so a caller that needs to find
		// the new column does not compute the name a second time.

		EditOutcome paste_value_list
		(
			const JsonPointer&            target,
			std::vector<PastedValue>&     values,
			const QString&                listName,
			PasteRoute                    route,
			const std::optional<QString>& targetColumnName = std::nullopt,
			QString*                      refusal          = nullptr,
			QString*                      pastedName       = nullptr
		);

		EditOutcome change_type ( const JsonPointer& target, JsonKind newKind );        // EDIT-09.

		EditOutcome normalize_array   ( const JsonPointer& target );                    // EDIT-11.
		EditOutcome array_to_objects  ( const JsonPointer& target );                    // EDIT-12.
		EditOutcome objects_to_array  ( const JsonPointer& target );                    // EDIT-13.

		// Generic wholesale replacement (Code View commit and the transforms above are built on this).

		EditOutcome replace_subtree ( const JsonPointer& target, std::unique_ptr<JsonNode> newSubtree, const QString& text );

		//=============================================================================================================
		// Placement inserts (Phase 9) -- insert a SPECIFIC node (not a neutral default) at a chosen position, as one
		// undoable step. These back the cell clipboard (EDITOR-11), the provisional-row materialization and
		// missing-member creation (EDITOR-12), and node paste (EDIT-06). VAL-02 is enforced on the object variants.
		//=============================================================================================================

	public:

		// Insert element into the array the pointer names, at index in [0, array_size]. Rejected if the pointer does not
		// resolve to an array or the index is out of range.

		EditOutcome insert_element_at ( const JsonPointer& arrayPointer, int index, std::unique_ptr<JsonNode> element, const QString& text );

		// Append element as the array's last element (insert_element_at at array_size).

		EditOutcome append_element ( const JsonPointer& arrayPointer, std::unique_ptr<JsonNode> element, const QString& text );

		// Insert a member into the object the pointer names, at index in [0, member_count], under key. Rejected if the
		// pointer does not resolve to an object, the index is out of range, or key already exists (VAL-02).

		EditOutcome insert_member_at ( const JsonPointer& objectPointer, int index, const QString& key, std::unique_ptr<JsonNode> value, const QString& text );

		// EDIT-06 node paste: place node relative to the selection using EDIT-03's rule -- a container receives it as its
		// last child, a scalar as the sibling immediately after itself. objectKey is used only when the receiving parent
		// is an object; empty means "synthesize a unique key". A colliding key is de-duplicated ("key (copy)"). Rejected
		// when the selection is the root scalar (no container to receive the paste and no sibling position).

		EditOutcome paste_node ( const JsonPointer& selection, std::unique_ptr<JsonNode> node, const QString& objectKey = QString () );

		//=============================================================================================================
		// Grouping (EDIT-14 / EDIT-10) -- several edits that undo as ONE step.
		//=============================================================================================================

	public:

		// Open a group; every edit made while it is alive undoes and redoes together. UNDO-02's "a Code View commit
		// undoes as one step" is the same principle, arrived at from the other direction.
		//
		// THE MACRO IS OPENED LAZILY, on the first edit that actually pushes a command, and that is the whole reason
		// this is a class rather than a begin / end pair on the controller. QUndoStack::beginMacro pushes its parent
		// command whether or not anything is ever added to it, so a group in which every edit turned out to be
		// Rejected or Unchanged would leave an undo step that undoes nothing -- exactly the defect Phase 15 found in
		// replace_subtree, where a no-op was pushed and reported as a success (lesson D19). A scope that never pushed
		// therefore ends having done nothing at all, which is what lets a caller open one before it knows whether the
		// work will happen.
		//
		// RAII rather than a begin / end pair because the callers are command handlers full of early returns, and an
		// unmatched beginMacro leaves the stack permanently inside a macro.
		//
		// NESTING IS SAFE AND MEANS NOTHING: a scope opened while another is alive neither opens a group nor ends
		// one, so the OUTER scope still decides where the single undo step begins and ends. That is what "one
		// command, one group" requires once a command is built from operations that are themselves grouped --
		// EDITOR-18's paste both assigns cells and appends elements, and each half was already a group of its own.
		// Before this, the inner scope's destructor closed the outer's macro early and every later push escaped it.

		class MacroScope
		{
		public:

			MacroScope ( UndoController& controller, const QString& text );
			~MacroScope ();

			MacroScope             ( const MacroScope& ) = delete;
			MacroScope& operator = ( const MacroScope& ) = delete;

			// Did anything actually reach the stack inside this scope? The multi-node commands ask so they can report
			// the difference between "all of it applied" and "none of it did" without counting outcomes themselves.

			bool pushed () const;

		private:

			UndoController& controller;

			// False when another scope was already alive, in which case this one does nothing at all.

			bool owned = true;
		};

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// The ONE way a command reaches the stack. Every push goes through here so the lazy macro above has a single
		// place to notice that a group has become non-empty -- a second push site would silently escape the grouping.

		void push_command ( QUndoCommand* command );

		// EDIT-16's array-of-objects case, split out because it is the only one of the four with two branches (a
		// column the array already carries, and one it does not) and a growth rule of its own.

		// EDIT-16's tree-route conversion: reshape a one-dimensional array into an array of objects, moving each bare
		// value under `existingName`, and then hand off to paste_column_into_objects for the new column. Two steps in one
		// macro, so the whole gesture is one undo step.

		void convert_array_and_paste_column
		(
			const JsonPointer&            arrayPointer,
			JsonNode*                     array,
			std::vector<PastedValue>&     values,
			const QString&                name,
			const QString&                existingName,
			const std::optional<QString>& insertBefore
		);

		// insertBefore names the column the new one goes IN FRONT OF; absent, the new column goes last, which is the
		// node route's placement.

		void paste_column_into_objects
		(
			const JsonPointer&            arrayPointer,
			JsonNode*                     array,
			std::vector<PastedValue>&     values,
			const QString&                name,
			const std::optional<QString>& insertBefore
		);

		static std::unique_ptr<JsonNode> make_default ( JsonKind kind );   // The neutral new-node value (EDIT-03).

		EditOutcome insert_at
		(
			const JsonPointer& parentPointer,
			JsonNode*          parentNode,
			int                index,
			const QString&     key,
			std::unique_ptr<JsonNode> node,
			const QString&     text
		);

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonDocument* document;                                    // Non-owning.

		bool          allowDuplicateKeys = false;                  // SET-03a, pushed in. VAL-02's default is to refuse.
		bool          replaying          = false;                  // UNDO-05: inside undo () / redo (), batch close included.
		QUndoStack    undoStack;

		// MacroScope's lazy state. macroText is what a macro will be called if one is ever opened; macroPending says a
		// scope is alive and has not needed one yet; macroOpen says beginMacro has been called and endMacro is owed.

		QString macroText;
		bool    macroPending = false;
		bool    macroOpen    = false;

		// undo () and redo () are the same replay in two directions.

		void replay ( bool forward );
	};
}

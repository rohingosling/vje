//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

#include <QObject>
#include <QString>
#include <QUndoStack>

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
		EditOutcome duplicate_node ( const JsonPointer& target );                       // EDIT-07.
		EditOutcome move_node      ( const JsonPointer& target, MoveDirection direction );  // EDIT-08.

		EditOutcome change_type ( const JsonPointer& target, JsonKind newKind );        // EDIT-09.

		EditOutcome normalize_array   ( const JsonPointer& target );                    // EDIT-11.
		EditOutcome array_to_objects  ( const JsonPointer& target );                    // EDIT-12.
		EditOutcome objects_to_array  ( const JsonPointer& target );                    // EDIT-13.

		// Generic wholesale replacement (Code View commit and the transforms above are built on this).

		EditOutcome replace_subtree ( const JsonPointer& target, std::unique_ptr<JsonNode> newSubtree, const QString& text );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

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
		QUndoStack    undoStack;
	};
}

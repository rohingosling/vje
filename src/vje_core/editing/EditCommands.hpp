//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   The edit-command family: one QUndoCommand per structural edit kind, all against a
//   JsonDocument. Every mutation the product makes is one of these commands, so command granularity is undo
//   granularity (UNDO-01..03). Commands do NOT touch the dirty flag -- that is derived from the QUndoStack clean
//   state by UndoController (UNDO-04); a command only mutates the tree and re-emits the document's node_changed()
//   signal on both redo and undo.
//
//   Targeting is by JsonPointer, re-resolved against the live tree on each redo/undo, so a command stays valid under
//   any ancestor edit applied earlier on the stack (the stack replays in order). A command that removes a node
//   retains ownership of it between redo and undo, keeping node addresses stable across the cycle.
//
//   Note on Qt module: QUndoCommand lives in Qt Gui in Qt 6 (not Qt Core), but it is a headless QObject-free class
//   that runs under a QCoreApplication -- vje_core links Qt6::Gui for it and remains display-free (see the top-level
//   CMakeLists comment).
//
//   The six kinds cover every EDIT-* requirement:
//     SetValueCommand    -- EDIT-01 in-place scalar value edit (same kind, node identity preserved).
//     RenameKeyCommand   -- EDIT-02 object member rename.
//     InsertNodeCommand  -- EDIT-03/04 add, EDIT-07 duplicate (insert a clone) -- backs any child insertion.
//     RemoveNodeCommand  -- EDIT-05 delete.
//     MoveNodeCommand    -- EDIT-08 reorder within a parent.
//     ReplaceNodeCommand -- EDIT-09 type change, EDIT-11..13 array transforms, and a Code View commit -- any
//                           wholesale node replacement, root included.
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

#include <QString>
#include <QUndoStack>          // Qt 6: declares both QUndoCommand and QUndoStack (Qt Gui module).

#include <memory>

namespace vje
{
	//*****************************************************************************************************************
	// Class: EditCommand -- shared base for the edit-command family.
	//*****************************************************************************************************************

	class EditCommand : public QUndoCommand
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		EditCommand ( JsonDocument* document, const QString& text );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	protected:

		JsonNode* node_at ( const JsonPointer& pointer ) const;    // Resolve against the live document; may be null.
		void      notify  ( const JsonPointer& pointer, DocumentChange change ) const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	protected:

		JsonDocument* document;                                    // Non-owning; outlives the command / the stack.
	};

	//*****************************************************************************************************************
	// Class: SetValueCommand -- EDIT-01, an in-place scalar value edit (same kind).
	//*****************************************************************************************************************

	class SetValueCommand : public EditCommand
	{
	public:

		// newScalar carries the new value; its kind must equal the target scalar's kind. The old value is captured
		// from the live node at construction.

		SetValueCommand ( JsonDocument* document, const JsonPointer& target, std::unique_ptr<JsonNode> newScalar );

		void redo () override;
		void undo () override;

	private:

		void assign ( const JsonNode& source ) const;             // Copy source's scalar value into the live target.

		JsonPointer               target;
		std::unique_ptr<JsonNode> oldScalar;
		std::unique_ptr<JsonNode> newScalar;
	};

	//*****************************************************************************************************************
	// Class: RenameKeyCommand -- EDIT-02.
	//*****************************************************************************************************************

	class RenameKeyCommand : public EditCommand
	{
	public:

		RenameKeyCommand ( JsonDocument* document, const JsonPointer& parentPointer, int index, const QString& newKey );

		void redo () override;
		void undo () override;

	private:

		JsonPointer parentPointer;                                 // The containing object.
		int         index;
		QString     oldKey;
		QString     newKey;
	};

	//*****************************************************************************************************************
	// Class: InsertNodeCommand -- EDIT-03/04 add, EDIT-07 duplicate.
	//*****************************************************************************************************************

	class InsertNodeCommand : public EditCommand
	{
	public:

		// key is used only when the parent is an object (ignored for an array). The command owns node until redo
		// installs it, and reclaims it on undo.

		InsertNodeCommand
		(
			JsonDocument*             document,
			const JsonPointer&        parentPointer,
			int                       index,
			bool                      parentIsObject,
			const QString&            key,
			std::unique_ptr<JsonNode> node
		);

		void redo () override;
		void undo () override;

	private:

		JsonPointer               parentPointer;
		int                       index;
		bool                      parentIsObject;
		QString                   key;
		std::unique_ptr<JsonNode> stashed;                         // Holds the node while it is out of the tree.
	};

	//*****************************************************************************************************************
	// Class: RemoveNodeCommand -- EDIT-05.
	//*****************************************************************************************************************

	class RemoveNodeCommand : public EditCommand
	{
	public:

		RemoveNodeCommand ( JsonDocument* document, const JsonPointer& parentPointer, int index, bool parentIsObject );

		void redo () override;
		void undo () override;

	private:

		JsonPointer               parentPointer;
		int                       index;
		bool                      parentIsObject;
		QString                   key;                             // Captured at construction (object parent only).
		std::unique_ptr<JsonNode> stashed;                         // Holds the removed node between redo and undo.
	};

	//*****************************************************************************************************************
	// Class: MoveNodeCommand -- EDIT-08.
	//*****************************************************************************************************************

	class MoveNodeCommand : public EditCommand
	{
	public:

		MoveNodeCommand ( JsonDocument* document, const JsonPointer& parentPointer, int fromIndex, int toIndex );

		void redo () override;
		void undo () override;

	private:

		JsonPointer parentPointer;
		int         fromIndex;
		int         toIndex;
	};

	//*****************************************************************************************************************
	// Class: ReplaceNodeCommand -- EDIT-09 type change, EDIT-11..13 transforms, Code View commit.
	//*****************************************************************************************************************

	class ReplaceNodeCommand : public EditCommand
	{
	public:

		// Replaces the node named by target with newNode. When target is the root, the swap goes through the
		// document's root. redo and undo are the same swap: each exchanges the installed node with the stashed one.

		ReplaceNodeCommand
		(
			JsonDocument*             document,
			const JsonPointer&        target,
			int                       childIndex,
			bool                      parentIsObject,
			bool                      targetIsRoot,
			std::unique_ptr<JsonNode> newNode,
			DocumentChange            change,
			const QString&            text
		);

		void redo () override;
		void undo () override;

	private:

		void swap ();                                              // The symmetric redo/undo operation.

		JsonPointer               target;
		int                       childIndex;
		bool                      parentIsObject;
		bool                      targetIsRoot;
		DocumentChange            change;
		std::unique_ptr<JsonNode> stashed;                         // The node not currently installed in the tree.
	};
}

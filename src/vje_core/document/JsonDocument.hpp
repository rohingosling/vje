//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonDocument -- owns the root JsonNode, the file path, and the dirty flag, and is the QObject through which the
//   tree model and editor views learn of changes. Phase 1 provides the whole-document
//   signals: reset() when the root is replaced (load / Code View commit), plus dirty_changed and file_path_changed.
//   Fine-grained node-change signals ride the edit commands and arrive with the editing phase.
//
//   It also keeps TREE-10's UNSAVED-CHANGE MARKS -- see "Change marks" below.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The kinds of fine-grained change an edit command reports through node_changed(), so the tree model (TREE-07) and
	// editor views (EDITOR-08) can patch incrementally instead of reloading. The pointer
	// carried alongside is: the affected node for an in-place change (ValueChanged / SubtreeReplaced), or the CONTAINING
	// node for anything that changes a child list or a child's name (KeyRenamed / NodeAdded / NodeRemoved / NodeMoved).
	//
	// KeyRenamed sits with the container group deliberately: a rename changes the member's own pointer, so naming it by
	// that pointer would be ambiguous between the pre- and post-rename form. Subscribers refresh the container's
	// children rather than one node.
	//-----------------------------------------------------------------------------------------------------------------

	enum class DocumentChange
	{
		ValueChanged,       // A scalar's value was edited in place; the node keeps its identity and kind (EDIT-01).
		KeyRenamed,         // An object member's key changed (EDIT-02); the pointer names the OBJECT, not the member.
		SubtreeReplaced,    // A node was replaced wholesale: type change, array transform, Code View commit (EDIT-09/11..13).
		NodeAdded,          // A child was inserted into the pointed-at container (EDIT-03/04/07).
		NodeRemoved,        // A child was removed from the pointed-at container (EDIT-05).
		NodeMoved           // A child was reordered within the pointed-at container (EDIT-08).
	};

	//*****************************************************************************************************************
	// Class: JsonDocument
	//*****************************************************************************************************************

	class JsonDocument : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonDocument ( QObject* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		JsonNode* root     () const;
		bool      has_root () const;

		const QString& file_path () const;
		bool           is_dirty  () const;

		// Resolve a pointer against the current root; nullptr for an empty document or an unresolvable pointer.

		JsonNode* resolve ( const JsonPointer& pointer ) const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_root      ( std::unique_ptr<JsonNode> newRoot );   // Load / full reset; emits reset().
		void set_file_path ( const QString& path );                 // Emits file_path_changed() on change.
		void set_dirty     ( bool dirty );                          // Emits dirty_changed() on change.

		// Editing-layer hooks (used by the edit commands). swap_root() exchanges the root without any signal -- the
		// command emits the precise node_changed(SubtreeReplaced) instead, so a root-level replace-subtree (e.g. a
		// Code View commit) is not conflated with a full reset() (NAV-03). notify_node_changed() re-emits the
		// fine-grained signal for a command's redo/undo.

		std::unique_ptr<JsonNode> swap_root           ( std::unique_ptr<JsonNode> newRoot );   // Returns the old root; no signal.
		void                      notify_node_changed ( const JsonPointer& pointer, DocumentChange change );

		//=============================================================================================================
		// Change batching (NFR-03)
		//
		// A MULTI-EDIT GESTURE IS ONE CHANGE, AND OBSERVERS SHOULD HEAR IT ONCE. Every edit command notifies as it is
		// applied, which is right for a single edit and quadratic for a group of them: a column delete of n elements
		// pushes n commands, and every observer of this document re-derives its whole projection n times. Measured on
		// a 400-element column with the Code tab open, that was 3.4 seconds, of which the edits themselves were 0.
		//
		// While a batch is open the notifications are COLLAPSED rather than dropped: their deepest common ancestor is
		// accumulated, and closing the batch emits exactly one node_changed naming that subtree. That is safe because
		// every observer re-derives by DIFFING against the live document rather than by applying a delta -- so n
		// notifications and one converge on the same projection.
		//
		// Nesting is counted, so an inner batch cannot close an outer one. UndoController::MacroScope opens one for
		// the length of a grouped command, and undo() / redo() open one for the replay -- which is where a group's
		// children are re-applied one at a time and would otherwise cost the same quadratic on the way back.
		//=============================================================================================================

	public:

		void begin_change_batch ();
		void end_change_batch   ();

		bool in_change_batch () const;

		//=============================================================================================================
		// Change marks (TREE-10)
		//
		// WHERE THE DOCUMENT HAS BEEN EDITED SINCE IT WAS LAST CLEAN -- accumulated, never diffed against the file,
		// which would mean keeping a second copy of the document. A node is marked when its change stamp equals
		// changeEpoch, so:
		//
		//   - MARKING is a stamp on the node and on each ancestor up to the root, made as the change ARRIVES. That is
		//     what makes "is there a change beneath me?" a read of one field at paint time rather than a walk of the
		//     subtree (NFR-03). The marks sit on the nodes rather than in a set of addresses because a set would keep
		//     the address of every node an edit has since destroyed, and a later node allocated at one of them would
		//     read as changed.
		//   - CLEARING is one increment of the epoch. Every stamp goes stale at once -- including those on the nodes an
		//     undo command is holding off the tree, which no walk from the root could reach.
		//
		// notify_node_changed marks the node its pointer names BEFORE a change batch collapses it, so a grouped gesture
		// marks exactly what each of its commands touched rather than their common ancestor. The commands add what
		// the pointer cannot name -- the child an insert placed, the member a rename renamed (mark_changed), and where
		// a replacement differs from what it replaced (mark_differences).
		//
		// The marks clear whenever the document becomes clean (set_dirty ( false )): a save, a load, and an undo or redo
		// arriving back at the saved point, which UndoController routes here from QUndoStack::cleanChanged (UNDO-04).
		//=============================================================================================================

	public:

		bool has_change_mark  ( const JsonNode* node ) const;       // Is this node marked in the current epoch?
		bool has_change_marks () const;                            // Is anything?

		// Mark a node and every ancestor up to the root. A null node marks nothing.

		void mark_changed ( JsonNode* node );

		// A REPLACEMENT'S marks: stamp every node of `after` that differs from its counterpart in `before`, and -- if
		// anything differs -- `after` and its ancestors. Arrays pair their elements by POSITION, which is how the tree
		// labels them; objects pair members by key, the nth occurrence with the nth, so a reordered member is not
		// itself changed although its object is. Returns whether anything differed.

		bool mark_differences ( const JsonNode& before, JsonNode& after );

		// Forget every mark. Emits change_marks_cleared () when there were any.

		void clear_change_marks ();

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void reset             ();
		void dirty_changed     ( bool dirty );
		void file_path_changed ( const QString& path );
		void node_changed      ( const JsonPointer& pointer, DocumentChange change );

		// Every change mark went at once (TREE-10). Not emitted by set_root, whose reset () already says every node is
		// new.

		void change_marks_cleared ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		std::unique_ptr<JsonNode> rootNode;
		QString                   filePath;
		bool                      dirtyState;

		// The change batch above. batchPointer names the subtree containing everything collapsed so far.

		int         changeBatchDepth = 0;
		bool        batchHasChange   = false;
		JsonPointer batchPointer;

		// The change marks above. The epoch starts at 1 because a node's stamp starts at 0, and 0 must never be current.

		std::uint64_t changeEpoch        = 1;
		bool          changeMarksPresent = false;
	};
}

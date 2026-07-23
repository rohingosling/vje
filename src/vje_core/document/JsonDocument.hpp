//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonDocument -- owns the root JsonNode, the file path, and the dirty flag, and is the QObject through which the
//   tree model and editor views learn of changes. Phase 1 provides the whole-document
//   signals: reset() when the root is replaced (load / Code View commit), plus dirty_changed and file_path_changed.
//   Fine-grained node-change signals ride the edit commands and arrive with the editing phase.
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
		// Signals
		//=============================================================================================================

	signals:

		void reset             ();
		void dirty_changed     ( bool dirty );
		void file_path_changed ( const QString& path );
		void node_changed      ( const JsonPointer& pointer, DocumentChange change );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		std::unique_ptr<JsonNode> rootNode;
		QString                   filePath;
		bool                      dirtyState;
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonTreeModel -- the QAbstractItemModel projecting a JsonDocument into the navigation tree (TREE-01..08).
//
//   THE PROJECTION. One item per document node, plus a ROOT item standing for the file itself (TREE-01): it is labelled
//   with the file name, always carries the document glyph whatever the root value's type is, and its children are the
//   root value's members / elements. So the root item and the root JSON Pointer ("") name the same thing, and the
//   status bar's pointer pane needs no special case.
//
//   Labels are KEYS ONLY (TREE-02): an object member shows its key, an array element shows "[index]". Scalar values are
//   never shown inline -- they belong to the Form / Code views -- but they ARE part of Qt::AccessibleTextRole, which is
//   what TREE-02 requires for screen readers.
//
//   WHY A SHADOW TREE RATHER THAN JsonNode* INDEXES. The obvious implementation puts JsonNode* straight into
//   QModelIndex::internalPointer. It does not survive contact with the change signals: JsonDocument reports a child-list
//   change AFTER the fact and names only the CONTAINER, so by the time the model hears about
//   a removal the node is already destroyed and there is nothing left to bracket a beginRemoveRows around. The model
//   therefore keeps its own TreeItem shadow of the nodes it has projected. The shadow is what QModelIndex points at, so
//   item identity is the model's own and outlives any node churn; on a change signal the model diffs the shadow against
//   the document and emits exactly the begin/end insert / remove / move pairs the diff implies. That is what makes the
//   updates genuinely incremental (TREE-07) instead of a reset that would throw away the user's expansion and position.
//
//   LAZY POPULATION (TREE-08). A container's children materialize on the first rowCount() -- which QTreeView only calls
//   for the root and for expanded branches -- so loading a large document projects one item, not a million. hasChildren()
//   answers from the node alone and never materializes, so the branch indicator costs nothing. Combined with the view's
//   own uniform-row-height virtualization, only visible rows are ever touched.
//
//   VIEW-STATE PRESERVATION. Most changes patch in place and QTreeView keeps expansion and selection by itself. The two
//   that cannot -- a root subtree replacement (a Code View commit) and a non-root subtree replacement -- are bracketed
//   by projection_about_to_rebuild() / projection_rebuilt() so the view can save and restore expansion and selection BY
//   POINTER (TREE-07, NAV-03). A document load is a different animal and deliberately does not emit
//   that pair: it goes through Qt's own modelReset, after which restoring the previous document's expansion would be
//   meaningless.
//
//   UNSAVED-CHANGE MARKS (TREE-10). The marks themselves are the document's (JsonDocument's "Change marks"); the model
//   answers CHANGE_MARK_ROLE from them, through the scope SET-14 chose, and says when an answer may have moved: along
//   the changed node's ancestors on every node_changed, and across every materialized row when the marks clear or the
//   scope changes. The answer is one read of the node's own stamp, so it costs the same however much lies beneath the
//   row (NFR-03). TreeNodeDelegate paints it.
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

#include "AppConfig.hpp"

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The private MIME type an EDIT-10 tree drag carries. Named once so the model's mimeData and canDropMimeData
	// cannot disagree about the string, and private so a drag from anywhere else -- a file from the desktop, text from
	// another application -- is refused by the format check alone and falls through to MainWindow's FILE-09 handler.
	//-----------------------------------------------------------------------------------------------------------------

	namespace tree_drag_mime
	{
		inline const QString VJE_TREE_ROWS = QStringLiteral ( "application/x-vje-tree-rows" );
	}

	class IconLibrary;

	//*****************************************************************************************************************
	// Class: JsonTreeModel
	//*****************************************************************************************************************

	class JsonTreeModel : public QAbstractItemModel
	{
		Q_OBJECT

		//=============================================================================================================
		// Types
		//=============================================================================================================

	private:

		// One projected node. Identity is the item's own address, which is what QModelIndex carries -- deliberately not
		// the JsonNode*, so an index stays valid across the node churn of a subtree replacement.
		//
		// rowIndex mirrors the item's position in its parent's child vector, which is also its member / element index
		// in the document. It is stored rather than searched for because parent() and the label both need it on every
		// data() call; JsonNode::index_in_parent() would make each of those a sibling scan.
		//
		// memberKey is the item's key in an OBJECT parent, stored on the shadow rather than read from the document.
		// That is a correctness requirement, not a cache: Qt's QItemSelectionModel moves a dying current row to a
		// sibling DURING beginRemoveRows -- after the document has shrunk (the change protocol reports after the fact)
		// but BEFORE the diff erases and renumbers the shadow -- and the selection bridge asks for that sibling's
		// pointer right then. Deriving the key by indexing the document with the stale shadow rowIndex answered with
		// the WRONG member's key, or walked off the end of the member list outright (a crash) when the removed row had
		// exactly one following sibling. The shadow must answer label and pointer queries from its own state at every
		// instant of a diff.

		struct TreeItem
		{
			JsonNode* node           = nullptr;   // The projected node (non-owning; re-pointed on a subtree replace).
			TreeItem* parentItem     = nullptr;   // Non-owning; nullptr for the root item.
			int       rowIndex       = 0;         // Position among the parent's children; 0 for the root item.
			QString   memberKey;                  // The key under an object parent; empty under an array / the root.
			bool      childrenLoaded = false;     // Whether children has been materialized (TREE-08).

			std::vector<std::unique_ptr<TreeItem>> children;
		};

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The icon library may be null -- the model then reports no decoration, which is the form the headless tests
		// use. It is observed for icons_changed so tree icons re-tint with the theme exactly as the menu and toolbar do.

		explicit JsonTreeModel ( JsonDocument* document, IconLibrary* icons = nullptr, QObject* parent = nullptr );

		//=============================================================================================================
		// Roles
		//=============================================================================================================

	public:

		// TREE-10: does this row carry the unsaved-change dot, under the current scope? A bool.

		static constexpr int CHANGE_MARK_ROLE = Qt::UserRole + 1;

		//=============================================================================================================
		// QAbstractItemModel
		//=============================================================================================================

	public:

		QModelIndex index  ( int row, int column, const QModelIndex& parent = QModelIndex () ) const override;
		QModelIndex parent ( const QModelIndex& child ) const override;

		int  rowCount    ( const QModelIndex& parent = QModelIndex () ) const override;
		int  columnCount ( const QModelIndex& parent = QModelIndex () ) const override;
		bool hasChildren ( const QModelIndex& parent = QModelIndex () ) const override;

		QVariant      data      ( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
		QVariant      headerData ( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;
		Qt::ItemFlags flags     ( const QModelIndex& index ) const override;

		//=============================================================================================================
		// Drag and drop (EDIT-10) -- DESCRIBING what may be dragged where, never performing the edit.
		//
		// The model answers the three questions Qt's drag machinery asks and stops there: which rows can start a drag,
		// what a drag carries, and whether a given landing place is legal. The reorder itself is an undoable command
		// raised by MainWindow, so nothing here mutates the document -- which is also why dropMimeData is NOT
		// overridden: the view never gets to perform the drop, and the default's refusal is the honest answer from a
		// model that does not do edits.
		//=============================================================================================================

	public:

		Qt::DropActions supportedDropActions () const override;
		QStringList     mimeTypes           () const override;
		QMimeData*      mimeData            ( const QModelIndexList& indexes ) const override;

		// EDIT-10's constraint, enforced BEFORE the drop rather than after it: a run of siblings may only land among
		// its own parent's children, between two rows. Refusing here is what withholds the drop indicator and shows
		// the platform's no-drop cursor, so an illegal drop is unreachable rather than reachable-and-rejected -- the
		// difference between a gesture the user can see will not work and one that silently does nothing.

		bool canDropMimeData
		(
			const QMimeData*   data,
			Qt::DropAction     action,
			int                row,
			int                column,
			const QModelIndex& parent
		) const override;

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The document node an index projects; nullptr for an invalid index.

		JsonNode* node_for_index ( const QModelIndex& index ) const;

		// The JSON Pointer naming an index. The root item yields the root pointer; an invalid index likewise.

		JsonPointer pointer_for_index ( const QModelIndex& index ) const;

		// The index a pointer names, MATERIALIZING the ancestors along the way (so it works for a branch the user has
		// never expanded -- Go To, Find, and expansion restore all depend on that). An invalid index if the pointer does
		// not resolve in the current document.

		QModelIndex index_for_pointer ( const JsonPointer& pointer ) const;

		// As above, but on a pointer that no longer resolves it falls back to the deepest ancestor that does, and to the
		// root item if none does. This is the NAV-03 rule for restoring selection after a whole-document commit. Returns
		// an invalid index only for an empty document.

		QModelIndex nearest_index_for_pointer ( const JsonPointer& pointer ) const;

		// The icon-set name for a JSON kind (TREE-03). Static and pure, so the mapping is unit-tested without a
		// QApplication, a palette, or the icon resource.

		static QString type_icon_name ( JsonKind kind );

		// SET-14: which rows CHANGE_MARK_ROLE marks. Pushed in by the window, which owns the setting; every materialized
		// row is re-asked at once, so switching needs no reload and restores whatever the edits had marked -- the marks
		// are kept whichever scope is showing them.

		void                          set_change_mark_scope ( config::tree::ChangeMarkScope scope );
		config::tree::ChangeMarkScope change_mark_scope     () const;

		// Is the model inside a row-removal window (between beginRemoveRows and the shadow's renumber)? Inside it an
		// ARRAY element's pointer_for_index answer is in PRE-removal coordinates -- Qt's model-index convention --
		// while the document is already post-removal, so a caller about to publish a pointer as a DOCUMENT coordinate
		// (the selection bridge) must wait for rowsRemoved instead. Exposed by the model because only the model knows;
		// a listener's own flag on rowsAboutToBeRemoved is raised too late (the selection model's fallback move runs
		// first, having connected at setModel time).

		bool removal_in_progress () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// Bracket a projection rebuild the view cannot follow on its own. The view saves expansion + selection by
		// pointer on the first and restores what still resolves on the second (TREE-07, NAV-03). Not emitted for a
		// document load, whose modelReset carries no expansion worth restoring.

		void projection_about_to_rebuild ();
		void projection_rebuilt          ();

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_document_reset ();
		void handle_node_changed   ( const JsonPointer& pointer, DocumentChange change );
		void handle_file_path_changed ();
		void handle_icons_changed  ();
		void handle_change_marks_cleared ();

		//=============================================================================================================
		// Helpers -- projection
		//=============================================================================================================

	private:

		void rebuild_root ();                                        // Discard and re-seed the shadow from the document.

		TreeItem* item_for_index ( const QModelIndex& index ) const;   // nullptr for an invalid index or empty document.
		QModelIndex index_for_item ( TreeItem* item ) const;           // The inverse; the root item maps to row 0.

		void materialize_children ( TreeItem* item ) const;            // Populate item->children once (TREE-08).

		// The materialized item a pointer names, WITHOUT materializing anything. Returns nullptr when the pointer names
		// a node inside a branch the model has never projected -- in which case there is nothing on screen to patch and
		// the caller correctly does nothing.

		TreeItem* find_materialized_item ( const JsonPointer& pointer ) const;

		//=============================================================================================================
		// Helpers -- incremental patching
		//=============================================================================================================

	private:

		void resync_children  ( TreeItem* item );                      // Diff shadow vs document; emit the exact signals.
		void replace_subtree  ( TreeItem* item, JsonNode* node );      // Re-point an item and drop its stale children.
		void renumber_children ( TreeItem* item );                     // Restore rowIndex after a structural patch.

		// Array element labels are POSITIONAL, so an insert or removal renames every sibling after it even though none
		// of them changed identity. Both the structural passes and a key rename end here.

		void refresh_child_labels ( TreeItem* item );

		void emit_item_changed ( TreeItem* item );                     // dataChanged over every role the label feeds.

		// TREE-10's repaints. A change marks the node its pointer names and every ancestor, so those rows are re-asked;
		// a clear or a scope change can move any row's answer, so every materialized one is -- a sibling range per
		// parent rather than a signal per row.

		void refresh_marks_along ( const JsonPointer& pointer );
		void refresh_all_marks   ();

		//=============================================================================================================
		// Helpers -- labels and description
		//=============================================================================================================

	private:

		QString label_for_item        ( TreeItem* item ) const;        // Key, "[index]", or the file name at the root.
		QString accessible_text_for_item ( TreeItem* item ) const;     // Label + type + scalar value (TREE-02).
		QString icon_name_for_item    ( TreeItem* item ) const;
		QString tooltip_for_item      ( TreeItem* item ) const;        // STYLE-17: pointer, type, value, mark.

		bool is_marked ( TreeItem* item ) const;                       // CHANGE_MARK_ROLE, under the current scope.

		// The child slot an RFC 6901 token names within a container, mirroring JsonPointer::resolve (an array token is
		// a canonical index; an object token is the FIRST member with that key, since duplicates are permitted on load).
		// -1 when the token names nothing.

		static int child_index_for_token ( const JsonNode* node, const QString& token );

		static int child_count ( const JsonNode* node );               // Elements, members, or 0 for a scalar.
		static JsonNode* child_at ( const JsonNode* node, int index );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonDocument* document;                                        // Non-owning.
		IconLibrary*  icons;                                           // Non-owning; may be null.

		// See removal_in_progress(). Raised before each beginRemoveRows and lowered after the matching endRemoveRows,
		// in both removal sites (the resync remove pass and a subtree replacement's child drop).

		bool removalInProgress = false;

		config::tree::ChangeMarkScope changeMarkScope = config::tree::DEFAULT_CHANGE_MARK_SCOPE;

		// The shadow root -- the item standing for the FILE (TREE-01). Null while the document has no root, which is
		// what makes the model report zero rows for an empty document.
		//
		// Mutable because materialization is lazy: rowCount() and index() are const by contract yet are exactly where a
		// branch first needs populating (TREE-08).

		mutable std::unique_ptr<TreeItem> rootItem;
	};
}

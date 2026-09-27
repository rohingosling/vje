//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonTreeModel implementation -- the shadow projection, its lazy population, and the incremental patching that keeps
//   it in step with the document's change signals. See the header for why the projection exists at all.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTreeModel.hpp"

#include "selection_summary.hpp"
#include "services/IconLibrary.hpp"
#include "style/tooltip_text.hpp"

#include <vje_core/services/JsonSerializer.hpp>

#include <QFileInfo>
#include <QIcon>
#include <QMimeData>

#include <algorithm>
#include <unordered_set>

namespace vje
{
	namespace
	{
		// The roles a label change touches. Kept in one place because every refresh path emits the same set, and a role
		// missing from one of them is the classic "the tree updated but the screen reader did not" bug. The change mark
		// is among them: a row is re-labelled because something about it changed, which is when its mark can move.

		const QList<int> LABEL_ROLES =
		{
			Qt::DisplayRole,
			Qt::DecorationRole,
			Qt::ToolTipRole,
			Qt::AccessibleTextRole,
			JsonTreeModel::CHANGE_MARK_ROLE
		};

		// The roles a change mark feeds (TREE-10): the dot, and the two texts that say it in words.

		const QList<int> MARK_ROLES =
		{
			JsonTreeModel::CHANGE_MARK_ROLE,
			Qt::ToolTipRole,
			Qt::AccessibleTextRole
		};

		// The document node's type as display text, for the tooltip and the accessible name.

		QString kind_text ( JsonKind kind )
		{
			switch ( kind )
			{
				case JsonKind::Object:  return QObject::tr ( "object" );
				case JsonKind::Array:   return QObject::tr ( "array" );
				case JsonKind::String:  return QObject::tr ( "string" );
				case JsonKind::Number:  return QObject::tr ( "number" );
				case JsonKind::Boolean: return QObject::tr ( "boolean" );
				case JsonKind::Null:    return QObject::tr ( "null" );
			}

			return QString ();
		}

		// A scalar's value as text. TREE-02 keeps this OUT of the tree label -- values are edited in the Form and Code
		// views -- but requires it in the node's accessible name, which is the only place it appears here.

		QString scalar_text ( const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::String:  return node.string_value ();
				case JsonKind::Number:  return node.number_token ();
				case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
				case JsonKind::Null:    return QStringLiteral ( "null" );

				default:                return QString ();
			}
		}

		// Whether a pointer token is a canonical array index ("0", or a non-zero-leading run of digits) -- the same rule
		// JsonPointer::resolve applies, restated here because the model resolves tokens to CHILD SLOTS rather than to
		// nodes.

		bool is_canonical_index ( const QString& token, int* outIndex )
		{
			if ( token.isEmpty () )
			{
				return false;
			}

			if ( ( token.size () > 1 ) && ( token.at ( 0 ) == QLatin1Char ( '0' ) ) )
			{
				return false;
			}

			bool      ok    = false;
			const int value = token.toInt ( &ok );

			if ( !ok || ( value < 0 ) )
			{
				return false;
			}

			*outIndex = value;

			return true;
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonTreeModel::JsonTreeModel ( JsonDocument* document, IconLibrary* icons, QObject* parent )
		: QAbstractItemModel ( parent )
		, document           ( document )
		, icons              ( icons )
	{
		rebuild_root ();

		connect ( document, &JsonDocument::reset,             this, &JsonTreeModel::handle_document_reset );
		connect ( document, &JsonDocument::node_changed,      this, &JsonTreeModel::handle_node_changed );
		connect ( document, &JsonDocument::file_path_changed, this, &JsonTreeModel::handle_file_path_changed );
		connect ( document, &JsonDocument::change_marks_cleared, this, &JsonTreeModel::handle_change_marks_cleared );

		// The root item is labelled with the FILE name (TREE-01), so a Save As has to relabel it.

		if ( icons != nullptr )
		{
			connect ( icons, &IconLibrary::icons_changed, this, &JsonTreeModel::handle_icons_changed );
		}
	}

	//=================================================================================================================
	// QAbstractItemModel
	//=================================================================================================================

	QModelIndex JsonTreeModel::index ( int row, int column, const QModelIndex& parent ) const
	{
		if ( ( column != 0 ) || ( row < 0 ) || ( rootItem == nullptr ) )
		{
			return QModelIndex ();
		}

		// The one top-level row is the file node itself.

		if ( !parent.isValid () )
		{
			return ( row == 0 ) ? createIndex ( 0, 0, rootItem.get () ) : QModelIndex ();
		}

		TreeItem* parentItem = item_for_index ( parent );

		if ( parentItem == nullptr )
		{
			return QModelIndex ();
		}

		materialize_children ( parentItem );

		if ( row >= static_cast<int> ( parentItem->children.size () ) )
		{
			return QModelIndex ();
		}

		return createIndex ( row, 0, parentItem->children [ static_cast<size_t> ( row ) ].get () );
	}

	QModelIndex JsonTreeModel::parent ( const QModelIndex& child ) const
	{
		TreeItem* childItem = item_for_index ( child );

		if ( ( childItem == nullptr ) || ( childItem->parentItem == nullptr ) )
		{
			return QModelIndex ();
		}

		return index_for_item ( childItem->parentItem );
	}

	int JsonTreeModel::rowCount ( const QModelIndex& parent ) const
	{
		if ( rootItem == nullptr )
		{
			return 0;
		}

		if ( !parent.isValid () )
		{
			return 1;
		}

		TreeItem* parentItem = item_for_index ( parent );

		if ( parentItem == nullptr )
		{
			return 0;
		}

		materialize_children ( parentItem );

		return static_cast<int> ( parentItem->children.size () );
	}

	int JsonTreeModel::columnCount ( const QModelIndex& ) const
	{
		return 1;
	}

	bool JsonTreeModel::hasChildren ( const QModelIndex& parent ) const
	{
		// Answered from the DOCUMENT, never by materializing (TREE-08). QTreeView calls this for every visible row to
		// decide whether to draw a branch indicator; the default implementation would call rowCount() and so populate
		// every visible branch whether or not the user ever expands it.

		if ( rootItem == nullptr )
		{
			return false;
		}

		if ( !parent.isValid () )
		{
			return true;
		}

		TreeItem* parentItem = item_for_index ( parent );

		return ( parentItem != nullptr ) && ( child_count ( parentItem->node ) > 0 );
	}

	QVariant JsonTreeModel::data ( const QModelIndex& index, int role ) const
	{
		TreeItem* item = item_for_index ( index );

		if ( item == nullptr )
		{
			return QVariant ();
		}

		switch ( role )
		{
			case Qt::DisplayRole:
			{
				return label_for_item ( item );
			}

			case Qt::DecorationRole:
			{
				return ( icons == nullptr ) ? QVariant () : QVariant ( icons->icon ( icon_name_for_item ( item ) ) );
			}

			case Qt::ToolTipRole:
			{
				return tooltip_for_item ( item );
			}

			case Qt::AccessibleTextRole:
			{
				return accessible_text_for_item ( item );
			}

			case CHANGE_MARK_ROLE:
			{
				return is_marked ( item );
			}

			default:
			{
				return QVariant ();
			}
		}
	}

	QVariant JsonTreeModel::headerData ( int section, Qt::Orientation orientation, int role ) const
	{
		// The pane hides the header; the label exists for accessibility tooling and for any future column.

		if ( ( orientation == Qt::Horizontal ) && ( section == 0 ) && ( role == Qt::DisplayRole ) )
		{
			return tr ( "Node" );
		}

		return QVariant ();
	}

	Qt::ItemFlags JsonTreeModel::flags ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return Qt::NoItemFlags;
		}

		// Selectable but NOT editable: renaming a key is a command with its own validation and undo step (EDIT-02,
		// VAL-02), not an in-place tree edit.

		Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

		// EDIT-10. A row can start a drag when it HAS a parent -- the document root has no siblings to reorder among,
		// which is the same guard delete, duplicate and move already apply. A row can RECEIVE one when it is a
		// container, since a drop lands between that container's children.

		JsonNode* const node = node_for_index ( index );

		if ( node != nullptr )
		{
			if ( node->parent () != nullptr )
			{
				itemFlags |= Qt::ItemIsDragEnabled;
			}

			if ( node->is_container () )
			{
				itemFlags |= Qt::ItemIsDropEnabled;
			}
		}

		return itemFlags;
	}

	//=================================================================================================================
	// Drag and drop (EDIT-10)
	//=================================================================================================================

	Qt::DropActions JsonTreeModel::supportedDropActions () const
	{
		// COPY, AND DELIBERATELY NOT MOVE, for a reason that is about the toolkit rather than about the gesture -- the
		// gesture is unambiguously a move. QAbstractItemView::startDrag removes the dragged rows through the model
		// when QDrag::exec returns Qt::MoveAction, which would tear the nodes out from under the undoable command
		// that is the actual reorder. Advertising only Copy makes MoveAction unreachable, so that branch provably
		// cannot run whatever the Qt version does inside it. See ReorderTreeView, which is the other half.

		return Qt::CopyAction;
	}

	QStringList JsonTreeModel::mimeTypes () const
	{
		return { tree_drag_mime::VJE_TREE_ROWS };
	}

	QMimeData* JsonTreeModel::mimeData ( const QModelIndexList& indexes ) const
	{
		if ( indexes.isEmpty () )
		{
			return nullptr;
		}

		// The payload is the dragged rows' PARENT and nothing else. What is being dragged is already known -- it is
		// the selection, which TREE-09 has confined to one parent -- so the only thing canDropMimeData cannot work
		// out for itself is which parent that was. Carrying the rows as well would be a second copy of the selection,
		// free to disagree with the live one by the time the drop lands.

		QMimeData* const data = new QMimeData ();

		data->setData ( tree_drag_mime::VJE_TREE_ROWS, pointer_for_index ( indexes.first ().parent () ).to_string ().toUtf8 () );

		return data;
	}

	bool JsonTreeModel::canDropMimeData
	(
		const QMimeData*   data,
		Qt::DropAction     action,
		int                row,
		int                column,
		const QModelIndex& parent
	) const
	{
		Q_UNUSED ( action )
		Q_UNUSED ( column )

		if ( ( data == nullptr ) || !data->hasFormat ( tree_drag_mime::VJE_TREE_ROWS ) )
		{
			return false;
		}

		// row == -1 is Qt's "onto this row" -- a re-parent, which EDIT-10 does not do. It has to be refused HERE
		// rather than at the drop, because it is also what the view reports while the pointer is over the middle of a
		// row, and refusing it is what turns the drop indicator off for that band.

		if ( ( row < 0 ) || !parent.isValid () )
		{
			return false;
		}

		return ( pointer_for_index ( parent ).to_string ().toUtf8 () == data->data ( tree_drag_mime::VJE_TREE_ROWS ) );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	JsonNode* JsonTreeModel::node_for_index ( const QModelIndex& index ) const
	{
		TreeItem* item = item_for_index ( index );

		return ( item == nullptr ) ? nullptr : item->node;
	}

	JsonPointer JsonTreeModel::pointer_for_index ( const QModelIndex& index ) const
	{
		TreeItem* item = item_for_index ( index );

		if ( item == nullptr )
		{
			return JsonPointer ();
		}

		// Walk up to the root item accumulating tokens, then reverse. The tokens come from the SHADOW alone -- the
		// stored member key, or the item's row for an element -- never by indexing the document with a shadow
		// position: during a removal diff the two disagree (see the TreeItem note), and Qt's selection model asks
		// this exact question inside beginRemoveRows when the dying row was current.

		QStringList tokens;
		QList<int>  occurrences;

		for ( TreeItem* walk = item; walk->parentItem != nullptr; walk = walk->parentItem )
		{
			JsonNode* parentNode = walk->parentItem->node;

			if ( parentNode == nullptr )
			{
				return JsonPointer ();
			}

			if ( parentNode->kind () == JsonKind::Object )
			{
				tokens.prepend ( walk->memberKey );

				// THE SHADOW KNOWS WHICH MEMBER THIS IS, and that is what makes a duplicate-keyed node addressable at
				// all: an object may carry `name` twice, a pointer names only the first, and every command routed by
				// pointer therefore acted on the first whatever the user had selected -- so deleting the second array
				// deleted the first (reported 2026-08-22).
				//
				// The occurrence is recorded ONLY where the key is duplicated (JsonPointer::occurrence_for), so a
				// pointer for an ordinary member is byte-for-byte the pointer it always was and every comparison
				// against a parsed one -- Go To, Find, the expansion restore -- is untouched.
				//
				// rowIndex rather than the document's own answer, for the reason this whole function reads the shadow:
				// during a removal diff the two disagree, and Qt asks this exact question inside beginRemoveRows.

				occurrences.prepend ( JsonPointer::occurrence_for ( *parentNode, walk->rowIndex ) );
			}
			else
			{
				tokens.prepend ( QString::number ( walk->rowIndex ) );

				occurrences.prepend ( -1 );                    // An array index is already unambiguous.
			}
		}

		JsonPointer result = JsonPointer::from_tokens ( tokens );

		for ( int index = 0; index < occurrences.size (); ++index )
		{
			if ( occurrences.at ( index ) >= 0 )
			{
				result = result.with_occurrence ( index, occurrences.at ( index ) );
			}
		}

		return result;
	}

	QModelIndex JsonTreeModel::index_for_pointer ( const JsonPointer& pointer ) const
	{
		if ( rootItem == nullptr )
		{
			return QModelIndex ();
		}

		TreeItem* item = rootItem.get ();

		for ( int tokenIndex = 0; tokenIndex < pointer.token_count (); ++tokenIndex )
		{
			materialize_children ( item );

			const int childIndex = child_index_for_token ( item->node, pointer.token ( tokenIndex ) );

			if ( ( childIndex < 0 ) || ( childIndex >= static_cast<int> ( item->children.size () ) ) )
			{
				return QModelIndex ();
			}

			item = item->children [ static_cast<size_t> ( childIndex ) ].get ();
		}

		return index_for_item ( item );
	}

	QModelIndex JsonTreeModel::nearest_index_for_pointer ( const JsonPointer& pointer ) const
	{
		if ( rootItem == nullptr )
		{
			return QModelIndex ();
		}

		// As index_for_pointer, but STOPS at the deepest ancestor that still resolves instead of failing -- the NAV-03
		// rule for restoring a selection after a whole-document commit reshaped the tree under it.

		TreeItem* item = rootItem.get ();

		for ( int tokenIndex = 0; tokenIndex < pointer.token_count (); ++tokenIndex )
		{
			materialize_children ( item );

			const int childIndex = child_index_for_token ( item->node, pointer.token ( tokenIndex ) );

			if ( ( childIndex < 0 ) || ( childIndex >= static_cast<int> ( item->children.size () ) ) )
			{
				break;
			}

			item = item->children [ static_cast<size_t> ( childIndex ) ].get ();
		}

		return index_for_item ( item );
	}

	void JsonTreeModel::set_change_mark_scope ( config::tree::ChangeMarkScope scope )
	{
		if ( scope == changeMarkScope )
		{
			return;
		}

		changeMarkScope = scope;

		refresh_all_marks ();
	}

	config::tree::ChangeMarkScope JsonTreeModel::change_mark_scope () const
	{
		return changeMarkScope;
	}

	QString JsonTreeModel::type_icon_name ( JsonKind kind )
	{
		switch ( kind )
		{
			case JsonKind::Object:  return icon_names::TYPE_OBJECT;
			case JsonKind::Array:   return icon_names::TYPE_ARRAY;
			case JsonKind::String:  return icon_names::TYPE_STRING;
			case JsonKind::Number:  return icon_names::TYPE_NUMBER;
			case JsonKind::Boolean: return icon_names::TYPE_BOOLEAN;
			case JsonKind::Null:    return icon_names::TYPE_NULL;
		}

		return QString ();
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void JsonTreeModel::handle_document_reset ()
	{
		// A load or a New: a different document entirely. Qt's own reset is the right signal, and deliberately NOT the
		// projection_about_to_rebuild pair -- there is no prior expansion worth restoring onto new content.

		beginResetModel ();

		rebuild_root ();

		endResetModel ();
	}

	void JsonTreeModel::handle_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		if ( rootItem == nullptr )
		{
			return;
		}

		switch ( change )
		{
			case DocumentChange::ValueChanged:
			{
				// Names the edited node itself. The label is key-only so it cannot have changed, but the accessible name
				// and the tooltip both carry the value.

				TreeItem* item = find_materialized_item ( pointer );

				if ( item != nullptr )
				{
					emit_item_changed ( item );
				}

				break;
			}

			case DocumentChange::KeyRenamed:
			{
				// Names the CONTAINING object, not the renamed member -- naming the member would be ambiguous between
				// its old and new pointer. The child set is unchanged, so only the labels need refreshing.

				TreeItem* container = find_materialized_item ( pointer );

				if ( ( container != nullptr ) && container->childrenLoaded )
				{
					refresh_child_labels ( container );
				}

				break;
			}

			case DocumentChange::SubtreeReplaced:
			{
				if ( pointer.is_root () )
				{
					// A Code View commit or a root type change. The shadow root's node is already dangling, so this can
					// only be a full rebuild -- bracketed so the view restores expansion and selection by pointer
					// (TREE-07, NAV-03).

					emit projection_about_to_rebuild ();

					beginResetModel ();

					rebuild_root ();

					endResetModel ();

					emit projection_rebuilt ();

					break;
				}

				TreeItem* item = find_materialized_item ( pointer );
				JsonNode* node = document->resolve ( pointer );

				if ( ( item == nullptr ) || ( node == nullptr ) )
				{
					break;
				}

				emit projection_about_to_rebuild ();

				replace_subtree ( item, node );

				emit projection_rebuilt ();

				break;
			}

			case DocumentChange::NodeAdded:
			case DocumentChange::NodeRemoved:
			case DocumentChange::NodeMoved:
			{
				// All three name the CONTAINER whose child list moved. Nothing to patch if the model has never projected
				// that branch -- it will be built correctly the first time the user expands it.

				TreeItem* container = find_materialized_item ( pointer );

				if ( container == nullptr )
				{
					break;
				}

				if ( container->childrenLoaded )
				{
					resync_children ( container );
				}
				else
				{
					// Unmaterialized, but its first or last child may have come or gone -- nudge the view to re-ask
					// hasChildren() so the branch indicator appears or disappears.

					emit_item_changed ( container );
				}

				break;
			}
		}

		// TREE-10. The document marked the node this pointer names and every ancestor before telling us, so those rows'
		// answers moved -- including ancestors no structural patch above touched. A root replacement has just reset the
		// whole projection, and re-asking it again would be harmless but pointless.

		if ( !( ( change == DocumentChange::SubtreeReplaced ) && pointer.is_root () ) )
		{
			refresh_marks_along ( pointer );
		}
	}

	void JsonTreeModel::handle_file_path_changed ()
	{
		// The root item is labelled with the file name (TREE-01), so a Save As relabels it.

		if ( rootItem != nullptr )
		{
			emit_item_changed ( rootItem.get () );
		}
	}

	void JsonTreeModel::handle_change_marks_cleared ()
	{
		// A save, or an undo back to the saved point: every mark went at once, so every row on screen may have lost one.

		refresh_all_marks ();
	}

	void JsonTreeModel::handle_icons_changed ()
	{
		// Every icon handed out is stale. Repainting the materialized items is enough: an unmaterialized branch fetches
		// its icons fresh the first time it is expanded.

		if ( rootItem == nullptr )
		{
			return;
		}

		std::vector<TreeItem*> pending { rootItem.get () };

		while ( !pending.empty () )
		{
			TreeItem* item = pending.back ();

			pending.pop_back ();

			emit_item_changed ( item );

			for ( const std::unique_ptr<TreeItem>& child : item->children )
			{
				pending.push_back ( child.get () );
			}
		}
	}

	//=================================================================================================================
	// Helpers -- projection
	//=================================================================================================================

	void JsonTreeModel::rebuild_root ()
	{
		rootItem.reset ();

		if ( !document->has_root () )
		{
			return;
		}

		rootItem = std::make_unique<TreeItem> ();

		rootItem->node = document->root ();
	}

	JsonTreeModel::TreeItem* JsonTreeModel::item_for_index ( const QModelIndex& index ) const
	{
		if ( !index.isValid () || ( index.model () != this ) )
		{
			return nullptr;
		}

		return static_cast<TreeItem*> ( index.internalPointer () );
	}

	QModelIndex JsonTreeModel::index_for_item ( TreeItem* item ) const
	{
		if ( item == nullptr )
		{
			return QModelIndex ();
		}

		// The root item is the single top-level row; every other item sits at its own slot in its parent.

		return createIndex ( ( item->parentItem == nullptr ) ? 0 : item->rowIndex, 0, item );
	}

	void JsonTreeModel::materialize_children ( TreeItem* item ) const
	{
		if ( item->childrenLoaded )
		{
			return;
		}

		item->childrenLoaded = true;

		const int count = child_count ( item->node );

		item->children.reserve ( static_cast<size_t> ( count ) );

		const bool parentIsObject = ( item->node != nullptr ) && ( item->node->kind () == JsonKind::Object );

		for ( int childIndex = 0; childIndex < count; ++childIndex )
		{
			auto child = std::make_unique<TreeItem> ();

			child->node       = child_at ( item->node, childIndex );
			child->parentItem = item;
			child->rowIndex   = childIndex;

			// The shadow's own copy of the key (see the TreeItem note). Read here, at materialization, when the
			// document and the shadow agree by construction.

			if ( parentIsObject )
			{
				child->memberKey = item->node->member_key ( childIndex );
			}

			item->children.push_back ( std::move ( child ) );
		}
	}

	JsonTreeModel::TreeItem* JsonTreeModel::find_materialized_item ( const JsonPointer& pointer ) const
	{
		if ( rootItem == nullptr )
		{
			return nullptr;
		}

		TreeItem* item = rootItem.get ();

		for ( int tokenIndex = 0; tokenIndex < pointer.token_count (); ++tokenIndex )
		{
			// Stop at the first unmaterialized branch rather than populating it: a change inside a branch the user has
			// never opened has nothing on screen to patch.

			if ( !item->childrenLoaded )
			{
				return nullptr;
			}

			const int childIndex = child_index_for_token ( item->node, pointer.token ( tokenIndex ) );

			if ( ( childIndex < 0 ) || ( childIndex >= static_cast<int> ( item->children.size () ) ) )
			{
				return nullptr;
			}

			item = item->children [ static_cast<size_t> ( childIndex ) ].get ();
		}

		return item;
	}

	//=================================================================================================================
	// Helpers -- incremental patching
	//=================================================================================================================

	void JsonTreeModel::resync_children ( TreeItem* item )
	{
		// Bring a materialized child list back into agreement with the document, emitting the exact model signals the
		// difference implies so QTreeView keeps expansion, selection, and scroll for every surviving row (TREE-07).
		//
		// The signals arrive AFTER the mutation, so this is a diff rather than a bracketed edit. Four passes: remove
		// what is gone, insert what is new, reorder what moved, then relabel -- the last because array element labels
		// are POSITIONAL, so an insert at the front renames every sibling after it.
		//
		// Shadow child nodes are compared by ADDRESS ONLY and never dereferenced: after a removal the shadow still holds
		// a pointer to the destroyed node, and reading through it would be undefined.

		const QModelIndex itemIndex          = index_for_item ( item );
		const int         documentChildCount = child_count ( item->node );

		// -- Pass 1: remove shadow items whose node is no longer a child, in contiguous runs (backwards, so an erase
		//            never shifts a slot still to be examined).

		std::unordered_set<const JsonNode*> liveChildren;

		for ( int childIndex = 0; childIndex < documentChildCount; ++childIndex )
		{
			liveChildren.insert ( child_at ( item->node, childIndex ) );
		}

		auto is_removed = [ &liveChildren, item ] ( int slot )
		{
			return liveChildren.count ( item->children [ static_cast<size_t> ( slot ) ]->node ) == 0;
		};

		int slot = static_cast<int> ( item->children.size () ) - 1;

		while ( slot >= 0 )
		{
			if ( !is_removed ( slot ) )
			{
				--slot;

				continue;
			}

			const int runEnd = slot;

			while ( ( slot >= 0 ) && is_removed ( slot ) )
			{
				--slot;
			}

			const int runStart = slot + 1;

			// The removal window (see removal_in_progress): Qt's selection model reacts INSIDE beginRemoveRows, and an
			// array element's pointer answer is pre-removal until renumber_children has run.

			removalInProgress = true;

			beginRemoveRows ( itemIndex, runStart, runEnd );

			item->children.erase
			(
				item->children.begin () + runStart,
				item->children.begin () + runEnd + 1
			);

			renumber_children ( item );

			endRemoveRows ();

			removalInProgress = false;
		}

		// -- Pass 2: insert items for document children the shadow does not hold, again in contiguous runs.

		std::unordered_set<const JsonNode*> shadowChildren;

		for ( const std::unique_ptr<TreeItem>& child : item->children )
		{
			shadowChildren.insert ( child->node );
		}

		int shadowCursor  = 0;
		int documentIndex = 0;

		while ( documentIndex < documentChildCount )
		{
			if ( shadowChildren.count ( child_at ( item->node, documentIndex ) ) != 0 )
			{
				++documentIndex;
				++shadowCursor;

				continue;
			}

			const int runStart = documentIndex;

			while ( ( documentIndex < documentChildCount ) &&
			        ( shadowChildren.count ( child_at ( item->node, documentIndex ) ) == 0 ) )
			{
				++documentIndex;
			}

			const int runLength = documentIndex - runStart;

			beginInsertRows ( itemIndex, shadowCursor, shadowCursor + runLength - 1 );

			for ( int offset = 0; offset < runLength; ++offset )
			{
				auto child = std::make_unique<TreeItem> ();

				child->node       = child_at ( item->node, runStart + offset );
				child->parentItem = item;

				// The diff runs AFTER the mutation, so the document position runStart + offset is current and its key
				// can be captured into the shadow (see the TreeItem note).

				if ( item->node->kind () == JsonKind::Object )
				{
					child->memberKey = item->node->member_key ( runStart + offset );
				}

				item->children.insert ( item->children.begin () + shadowCursor + offset, std::move ( child ) );
			}

			renumber_children ( item );

			endInsertRows ();

			shadowCursor += runLength;
		}

		// -- Pass 3: reorder. After passes 1 and 2 the two lists hold the same nodes, so a selection sort over at most
		//            one out-of-place run settles it -- and a plain reorder is one move, not a remove plus an insert,
		//            which is what lets the view carry the moved row's expansion with it.

		for ( int target = 0; target < documentChildCount; ++target )
		{
			JsonNode* wanted = child_at ( item->node, target );

			if ( item->children [ static_cast<size_t> ( target ) ]->node == wanted )
			{
				continue;
			}

			int source = -1;

			for ( int search = target + 1; search < static_cast<int> ( item->children.size () ); ++search )
			{
				if ( item->children [ static_cast<size_t> ( search ) ]->node == wanted )
				{
					source = search;

					break;
				}
			}

			if ( source < 0 )
			{
				continue;
			}

			beginMoveRows ( itemIndex, source, source, itemIndex, target );

			std::rotate
			(
				item->children.begin () + target,
				item->children.begin () + source,
				item->children.begin () + source + 1
			);

			renumber_children ( item );

			endMoveRows ();
		}

		// -- Pass 4: relabel. Element labels are their positions, so anything after an insertion or removal now reads
		//            differently even though its identity never changed.

		refresh_child_labels ( item );

		emit_item_changed ( item );
	}

	void JsonTreeModel::replace_subtree ( TreeItem* item, JsonNode* node )
	{
		// The node behind this item was swapped out wholesale (a type change, an array transform). Re-point FIRST so
		// nothing reached from the row-removal signals can read through the dangling old node, then drop the stale
		// children; the view re-populates on demand and the pane restores the expansion around us.

		item->node = node;

		if ( item->childrenLoaded && !item->children.empty () )
		{
			const QModelIndex itemIndex = index_for_item ( item );

			// The removal window again (see removal_in_progress) -- a whole-children drop is still a removal a
			// selection fallback can land inside.

			removalInProgress = true;

			beginRemoveRows ( itemIndex, 0, static_cast<int> ( item->children.size () ) - 1 );

			item->children.clear ();

			endRemoveRows ();

			removalInProgress = false;
		}

		item->childrenLoaded = false;

		emit_item_changed ( item );
	}

	bool JsonTreeModel::removal_in_progress () const
	{
		return removalInProgress;
	}

	void JsonTreeModel::renumber_children ( TreeItem* item )
	{
		for ( size_t childIndex = 0; childIndex < item->children.size (); ++childIndex )
		{
			item->children [ childIndex ]->rowIndex = static_cast<int> ( childIndex );
		}
	}

	void JsonTreeModel::refresh_child_labels ( TreeItem* item )
	{
		if ( item->children.empty () )
		{
			return;
		}

		// Re-capture the stored member keys first (see the TreeItem note). Both callers guarantee the shadow and the
		// document agree in SHAPE here -- resync_children calls this after its structural passes, and a KeyRenamed
		// changes no child count -- so reading the key at the child's own slot is exact. This is also what makes a
		// rename reach the stored copy: KeyRenamed funnels here.

		if ( ( item->node != nullptr ) && ( item->node->kind () == JsonKind::Object ) )
		{
			for ( size_t childIndex = 0; childIndex < item->children.size (); ++childIndex )
			{
				item->children [ childIndex ]->memberKey = item->node->member_key ( static_cast<int> ( childIndex ) );
			}
		}

		const QModelIndex itemIndex = index_for_item ( item );
		const int         lastRow   = static_cast<int> ( item->children.size () ) - 1;

		emit dataChanged ( index ( 0, 0, itemIndex ), index ( lastRow, 0, itemIndex ), LABEL_ROLES );
	}

	void JsonTreeModel::emit_item_changed ( TreeItem* item )
	{
		const QModelIndex itemIndex = index_for_item ( item );

		emit dataChanged ( itemIndex, itemIndex, LABEL_ROLES );
	}

	void JsonTreeModel::refresh_marks_along ( const JsonPointer& pointer )
	{
		if ( rootItem == nullptr )
		{
			return;
		}

		// Down the materialized path only, as find_materialized_item walks it: a row that was never projected is asked
		// afresh when it first is, so it has nothing stale to repaint.

		TreeItem* item = rootItem.get ();

		for ( int tokenIndex = 0; ; ++tokenIndex )
		{
			const QModelIndex itemIndex = index_for_item ( item );

			emit dataChanged ( itemIndex, itemIndex, MARK_ROLES );

			if ( ( tokenIndex >= pointer.token_count () ) || !item->childrenLoaded )
			{
				return;
			}

			const int childIndex = child_index_for_token ( item->node, pointer.token ( tokenIndex ) );

			if ( ( childIndex < 0 ) || ( childIndex >= static_cast<int> ( item->children.size () ) ) )
			{
				return;
			}

			item = item->children [ static_cast<size_t> ( childIndex ) ].get ();
		}
	}

	void JsonTreeModel::refresh_all_marks ()
	{
		if ( rootItem == nullptr )
		{
			return;
		}

		const QModelIndex rootIndex = index_for_item ( rootItem.get () );

		emit dataChanged ( rootIndex, rootIndex, MARK_ROLES );

		std::vector<TreeItem*> pending { rootItem.get () };

		while ( !pending.empty () )
		{
			TreeItem* const item = pending.back ();

			pending.pop_back ();

			if ( !item->childrenLoaded || item->children.empty () )
			{
				continue;
			}

			const QModelIndex itemIndex = index_for_item ( item );
			const int         lastRow   = static_cast<int> ( item->children.size () ) - 1;

			emit dataChanged ( index ( 0, 0, itemIndex ), index ( lastRow, 0, itemIndex ), MARK_ROLES );

			for ( const std::unique_ptr<TreeItem>& child : item->children )
			{
				pending.push_back ( child.get () );
			}
		}
	}

	//=================================================================================================================
	// Helpers -- labels and description
	//=================================================================================================================

	QString JsonTreeModel::label_for_item ( TreeItem* item ) const
	{
		// The root item stands for the FILE, so it is labelled with the file name whatever the root value's type is
		// (TREE-01); an unsaved document has no name yet.

		if ( item->parentItem == nullptr )
		{
			const QString& filePath = document->file_path ();

			return filePath.isEmpty () ? tr ( "Untitled" ) : QFileInfo ( filePath ).fileName ();
		}

		JsonNode* parentNode = item->parentItem->node;

		if ( parentNode == nullptr )
		{
			return QString ();
		}

		// Keys only (TREE-02): an object member shows its key, an array element its bracketed index. The key is the
		// SHADOW's stored copy, for the same reason pointer_for_index reads it: a paint or accessibility query landing
		// inside a removal diff must not index the already-mutated document with a stale shadow position.

		if ( parentNode->kind () == JsonKind::Object )
		{
			return item->memberKey;
		}

		return QStringLiteral ( "[%1]" ).arg ( item->rowIndex );
	}

	QString JsonTreeModel::accessible_text_for_item ( TreeItem* item ) const
	{
		// TREE-02: the scalar value is not shown inline, but it stays part of the node's accessible name -- otherwise a
		// screen-reader user hears the key and never the value.

		const QString label = label_for_item ( item );
		const QString type  = kind_text ( item->node->kind () );

		// TREE-10: the dot is a colour and a shape, which a screen reader has no way to say. Last, so the name a user
		// hears first is still the node's own.

		const QString marked = is_marked ( item ) ? tr ( ", unsaved changes" ) : QString ();

		if ( item->node->is_scalar () )
		{
			return tr ( "%1, %2, %3" ).arg ( label, type, scalar_text ( *item->node ) ) + marked;
		}

		const int count = child_count ( item->node );

		return ( ( item->node->kind () == JsonKind::Object )
			? tr ( "%1, object, %n member(s)", nullptr, count ).arg ( label )
			: tr ( "%1, array, %n item(s)",    nullptr, count ).arg ( label ) ) + marked;
	}

	QString JsonTreeModel::tooltip_for_item ( TreeItem* item ) const
	{
		// STYLE-17: what the row does not already show. The label is the key, so the tooltip carries WHERE the node is,
		// WHAT it is, and -- for a leaf, whose value TREE-02 keeps out of the label -- what it holds.
		//
		// The first line is the JSON Pointer, which is what a Go To takes (FIND-04) and what an error names. The file
		// node's pointer is empty, so it gives the file's full path instead: its label is the name alone, and a name
		// alone is ambiguous across folders (the Recent Files rule, FILE-05).

		QStringList lines;

		if ( item->parentItem == nullptr )
		{
			const QString& filePath = document->file_path ();

			lines.append ( filePath.isEmpty () ? tr ( "Untitled" ) : filePath );
		}
		else
		{
			lines.append ( pointer_for_index ( index_for_item ( item ) ).to_string () );
		}

		// The type in the status bar's own words ("object \u00B7 5 members"), so the two never describe one node two ways.

		lines.append ( selection_summary ( item->node, 1 ) );

		// A leaf's value, as JSON: a string quoted and escaped, so a trailing space or a line break is visible rather
		// than lost at the end of the line.

		if ( item->node->is_scalar () )
		{
			lines.append ( JsonSerializer::serialize ( *item->node ) );
		}

		if ( is_marked ( item ) )
		{
			lines.append ( tr ( "Unsaved changes" ) );
		}

		return tooltip_text ( lines.join ( QLatin1Char ( '\n' ) ) );
	}

	bool JsonTreeModel::is_marked ( TreeItem* item ) const
	{
		// SET-14. Under FileNodeOnly the file node answers for the whole document; since every mark reaches the root, the
		// root's own stamp IS "anything changed", so both scopes read one stamp and neither walks anything.

		if ( ( changeMarkScope == config::tree::ChangeMarkScope::FileNodeOnly ) && ( item->parentItem != nullptr ) )
		{
			return false;
		}

		return document->has_change_mark ( item->node );
	}

	QString JsonTreeModel::icon_name_for_item ( TreeItem* item ) const
	{
		// The root keeps the document glyph regardless of the root value's type (TREE-01); below it the icon is the
		// node's own JSON kind wherever it appears (TREE-03).

		return ( item->parentItem == nullptr ) ? icon_names::TYPE_DOCUMENT : type_icon_name ( item->node->kind () );
	}

	int JsonTreeModel::child_index_for_token ( const JsonNode* node, const QString& token )
	{
		if ( node == nullptr )
		{
			return -1;
		}

		if ( node->kind () == JsonKind::Array )
		{
			int elementIndex = 0;

			if ( !is_canonical_index ( token, &elementIndex ) || ( elementIndex >= node->array_size () ) )
			{
				return -1;
			}

			return elementIndex;
		}

		if ( node->kind () == JsonKind::Object )
		{
			// First match wins, mirroring JsonPointer::resolve -- a file may be loaded with duplicate sibling keys
			// (VAL-02), and the projection must name the same member the pointer does.

			for ( int memberIndex = 0; memberIndex < node->member_count (); ++memberIndex )
			{
				if ( node->member_key ( memberIndex ) == token )
				{
					return memberIndex;
				}
			}
		}

		return -1;
	}

	int JsonTreeModel::child_count ( const JsonNode* node )
	{
		if ( node == nullptr )
		{
			return 0;
		}

		switch ( node->kind () )
		{
			case JsonKind::Array:  return node->array_size ();
			case JsonKind::Object: return node->member_count ();

			default:               return 0;
		}
	}

	JsonNode* JsonTreeModel::child_at ( const JsonNode* node, int index )
	{
		if ( node == nullptr )
		{
			return nullptr;
		}

		switch ( node->kind () )
		{
			case JsonKind::Array:  return node->array_element ( index );
			case JsonKind::Object: return node->member_value ( index );

			default:               return nullptr;
		}
	}
}

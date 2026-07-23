//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonTableModel implementation -- the projection, the incremental diff, and the edit routing. See the header for the
//   column-projection rule and why rows are diffed by node address.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTableModel.hpp"

#include "models/cell_presentation.hpp"

#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/Validator.hpp>

#include <QSet>

#include <algorithm>

namespace vje
{
	namespace
	{
		// The header of the single value column, when the array's own name gives nothing to use (a root array).

		const QString ROOT_VALUE_COLUMN_TITLE = QStringLiteral ( "value" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonTableModel::JsonTableModel ( JsonDocument* document, UndoController* undo, QObject* parent )
		: QAbstractTableModel ( parent )
		, document ( document )
		, undo     ( undo )
	{
		connect ( document, &JsonDocument::reset,        this, &JsonTableModel::handle_document_reset );
		connect ( document, &JsonDocument::node_changed, this, &JsonTableModel::handle_node_changed );
	}

	//=================================================================================================================
	// QAbstractTableModel
	//=================================================================================================================

	int JsonTableModel::rowCount ( const QModelIndex& parent ) const
	{
		// A table model has rows only at the top level; a valid parent means a caller is treating it as a tree.

		if ( parent.isValid () )
		{
			return 0;
		}

		return static_cast<int> ( rowNodes.size () );
	}

	int JsonTableModel::columnCount ( const QModelIndex& parent ) const
	{
		if ( parent.isValid () || ( arrayNode == nullptr ) )
		{
			return 0;
		}

		// Single-value mode still has one column even for an empty array, so the table presents a real (if empty) grid
		// rather than collapsing to nothing.

		return objectMode ? static_cast<int> ( columnKeys.size () ) : 1;
	}

	QVariant JsonTableModel::data ( const QModelIndex& index, int role ) const
	{
		if ( !index.isValid () )
		{
			return QVariant ();
		}

		JsonNode* const node = node_for_cell ( index.row (), index.column () );

		switch ( role )
		{
			case Qt::DisplayRole:
			{
				return cell_display_text ( node );
			}

			case Qt::EditRole:
			{
				return cell_edit_text ( node );
			}

			case Qt::ToolTipRole:
			{
				// The column cap (config::form::MAXIMUM_COLUMN_WIDTH) means a long value is routinely elided, so the
				// tooltip is how the whole of it stays reachable without opening an editor.

				return is_editable_cell ( node ) ? cell_display_text ( node ) : QVariant ();
			}

			case Qt::TextAlignmentRole:
			{
				return QVariant::fromValue ( static_cast<int> ( Qt::AlignLeft | Qt::AlignVCenter ) );
			}

			case cell_roles::CONTENT_KIND:
			{
				return static_cast<int> ( cell_content ( node ) );
			}

			case cell_roles::VALUE_KIND:
			{
				return ( node != nullptr ) ? QVariant ( static_cast<int> ( node->kind () ) ) : QVariant ();
			}

			default:
			{
				return QVariant ();
			}
		}
	}

	QVariant JsonTableModel::headerData ( int section, Qt::Orientation orientation, int role ) const
	{
		if ( role != Qt::DisplayRole )
		{
			return QVariant ();
		}

		if ( orientation == Qt::Vertical )
		{
			// Element indices, matching the "[0]" / "[1]" labels the tree shows for the same elements (TREE-02), so the
			// two panes name a row the same way.

			return QString::number ( section );
		}

		if ( objectMode )
		{
			const bool inRange = ( section >= 0 ) && ( section < columnKeys.size () );

			return inRange ? QVariant ( columnKeys.at ( section ) ) : QVariant ();
		}

		// Single-value mode: name the column after the array itself, which is more use than a generic label -- "roles"
		// rather than "value". Only a root array has no name to borrow.

		return arrayPointer.is_root () ? ROOT_VALUE_COLUMN_TITLE
		                               : arrayPointer.token ( arrayPointer.token_count () - 1 );
	}

	Qt::ItemFlags JsonTableModel::flags ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return Qt::NoItemFlags;
		}

		// EVERY cell is selectable, including null, missing, and container cells -- the landability rule (EDITOR-03,
		// grid_navigation.hpp). Only the scalar cells are editable; a container drills in and a null placeholder is
		// read-only until EDITOR-12's typed entry lands in Phase 9.

		Qt::ItemFlags cellFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

		if ( ( undo != nullptr ) && is_editable_cell ( node_for_cell ( index.row (), index.column () ) ) )
		{
			cellFlags |= Qt::ItemIsEditable;
		}

		return cellFlags;
	}

	bool JsonTableModel::setData ( const QModelIndex& index, const QVariant& value, int role )
	{
		if ( !index.isValid () || ( role != Qt::EditRole ) || ( undo == nullptr ) )
		{
			return false;
		}

		JsonNode* const target = node_for_cell ( index.row (), index.column () );

		return commit_cell ( target, pointer_for_cell ( index.row (), index.column () ), value );
	}

	//=================================================================================================================
	// Presentation
	//=================================================================================================================

	void JsonTableModel::present ( const JsonPointer& pointer )
	{
		JsonNode* const resolved = document->resolve ( pointer );

		if ( ( resolved == nullptr ) || ( resolved->kind () != JsonKind::Array ) )
		{
			clear_presentation ();

			return;
		}

		beginResetModel ();

		arrayPointer = pointer;
		arrayNode    = resolved;

		objectMode = derive_object_mode ();
		columnKeys = derive_columns ();

		capture_rows ();

		endResetModel ();
	}

	void JsonTableModel::clear_presentation ()
	{
		if ( arrayNode == nullptr )
		{
			return;
		}

		beginResetModel ();

		arrayPointer = JsonPointer ();
		arrayNode    = nullptr;
		objectMode   = false;

		rowNodes  .clear ();
		columnKeys.clear ();

		endResetModel ();
	}

	bool JsonTableModel::is_presenting () const
	{
		return arrayNode != nullptr;
	}

	const JsonPointer& JsonTableModel::presented_pointer () const
	{
		return arrayPointer;
	}

	bool JsonTableModel::is_object_table () const
	{
		return objectMode;
	}

	//=================================================================================================================
	// Cell Addressing
	//=================================================================================================================

	JsonNode* JsonTableModel::node_for_cell ( int row, int column ) const
	{
		const bool rowInRange = ( row >= 0 ) && ( row < static_cast<int> ( rowNodes.size () ) );

		if ( !rowInRange )
		{
			return nullptr;
		}

		JsonNode* const element = rowNodes [ static_cast<size_t> ( row ) ];

		if ( !objectMode )
		{
			return ( column == 0 ) ? element : nullptr;
		}

		const bool columnInRange = ( column >= 0 ) && ( column < columnKeys.size () );

		if ( !columnInRange || ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
		{
			return nullptr;
		}

		// find_member returns null for a key this element lacks, which is precisely the MISSING cell of a ragged array
		// (EDITOR-03) -- so the ragged case needs no branch of its own here.

		return element->find_member ( columnKeys.at ( column ) );
	}

	JsonPointer JsonTableModel::pointer_for_cell ( int row, int column ) const
	{
		const bool rowInRange = ( row >= 0 ) && ( row < static_cast<int> ( rowNodes.size () ) );

		if ( !rowInRange )
		{
			return JsonPointer ();
		}

		const JsonPointer elementPointer = arrayPointer.child ( QString::number ( row ) );

		if ( !objectMode )
		{
			return elementPointer;
		}

		const bool columnInRange = ( column >= 0 ) && ( column < columnKeys.size () );

		// The MISSING cell keeps the pointer its member WOULD occupy, so a create-the-member paste has an address to
		// aim at (EDITOR-11, Phase 9).

		return columnInRange ? elementPointer.child ( columnKeys.at ( column ) ) : elementPointer;
	}

	GridPosition JsonTableModel::cell_for_pointer ( const JsonPointer& pointer ) const
	{
		if ( ( arrayNode == nullptr ) || !covers ( pointer ) || ( pointer == arrayPointer ) )
		{
			return GridPosition {};
		}

		const int prefixLength   = arrayPointer.token_count ();
		const int relativeLength = pointer.token_count () - prefixLength;

		// A cell sits exactly one level below the array in single-value mode, two in object mode. Anything deeper is
		// INSIDE a container cell rather than being one.

		const int expectedLength = objectMode ? 2 : 1;

		if ( relativeLength != expectedLength )
		{
			return GridPosition {};
		}

		bool      indexIsNumeric = false;
		const int row            = pointer.token ( prefixLength ).toInt ( &indexIsNumeric );

		const bool rowInRange = indexIsNumeric && ( row >= 0 ) && ( row < static_cast<int> ( rowNodes.size () ) );

		if ( !rowInRange )
		{
			return GridPosition {};
		}

		if ( !objectMode )
		{
			return GridPosition { row, 0 };
		}

		const int column = columnKeys.indexOf ( pointer.token ( prefixLength + 1 ) );

		return ( column >= 0 ) ? GridPosition { row, column } : GridPosition {};
	}

	//=================================================================================================================
	// IGridProjection
	//=================================================================================================================

	JsonNode* JsonTableModel::grid_node ( int row, int column ) const
	{
		return node_for_cell ( row, column );
	}

	JsonPointer JsonTableModel::grid_pointer ( int row, int column ) const
	{
		return pointer_for_cell ( row, column );
	}

	GridPosition JsonTableModel::grid_cell ( const JsonPointer& pointer ) const
	{
		return cell_for_pointer ( pointer );
	}

	GridPosition JsonTableModel::grid_edit_cell ( int row, int column ) const
	{
		return GridPosition { row, column };
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void JsonTableModel::handle_document_reset ()
	{
		// A load replaces the whole document, so whatever was projected is gone by definition. The Form View presents
		// again from the new selection.

		clear_presentation ();
	}

	void JsonTableModel::handle_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		if ( arrayNode == nullptr )
		{
			return;
		}

		// Re-resolve first, unconditionally. A replacement ANYWHERE at or above the projected array swaps the node out
		// from under us, and the signal names the replaced node rather than ours -- so comparing addresses is the only
		// reliable way to notice, and it costs one pointer walk.

		JsonNode* const resolved = document->resolve ( arrayPointer );

		if ( ( resolved == nullptr ) || ( resolved->kind () != JsonKind::Array ) )
		{
			clear_presentation ();

			return;
		}

		if ( resolved != arrayNode )
		{
			arrayNode = resolved;

			rebuild ();

			return;
		}

		if ( !covers ( pointer ) )
		{
			return;
		}

		// A scalar edit is the common case and the one that must stay cheap: patch exactly the cell it touched, so the
		// table refreshes in place with no column re-measure and no lost current cell (EDITOR-03).

		if ( change == DocumentChange::ValueChanged )
		{
			const GridPosition cell = enclosing_cell ( pointer );

			if ( cell.is_valid () )
			{
				const QModelIndex changedIndex = index ( cell.row, cell.column );

				emit dataChanged ( changedIndex, changedIndex );

				return;
			}
		}

		resync ();
	}

	//=================================================================================================================
	// Helpers -- projection
	//=================================================================================================================

	void JsonTableModel::rebuild ()
	{
		beginResetModel ();

		objectMode = derive_object_mode ();
		columnKeys = derive_columns ();

		capture_rows ();

		endResetModel ();
	}

	void JsonTableModel::resync ()
	{
		// A change of column MODE is not patchable: every cell addresses something different afterwards, so the honest
		// response is a reset. It is also rare -- it takes an edit that makes the last non-object element an object, or
		// the reverse.

		if ( derive_object_mode () != objectMode )
		{
			rebuild ();

			return;
		}

		if ( !resync_columns () )
		{
			rebuild ();

			return;
		}

		resync_rows ();

		// Element labels in the vertical header are POSITIONAL, so an insert or removal renames every row after it even
		// though none of them changed identity -- the same trap JsonTreeModel's relabel pass exists for.

		const int lastRow    = rowCount ()    - 1;
		const int lastColumn = columnCount () - 1;

		if ( ( lastRow >= 0 ) && ( lastColumn >= 0 ) )
		{
			emit dataChanged ( index ( 0, 0 ), index ( lastRow, lastColumn ) );

			emit headerDataChanged ( Qt::Vertical, 0, lastRow );
		}
	}

	void JsonTableModel::capture_rows ()
	{
		rowNodes.clear ();

		const int elementCount = arrayNode->array_size ();

		rowNodes.reserve ( static_cast<size_t> ( elementCount ) );

		for ( int elementIndex = 0; elementIndex < elementCount; ++elementIndex )
		{
			rowNodes.push_back ( arrayNode->array_element ( elementIndex ) );
		}
	}

	QStringList JsonTableModel::derive_columns () const
	{
		if ( !objectMode )
		{
			return QStringList ();
		}

		// The union of every element's keys in FIRST-ENCOUNTERED order (EDITOR-03), which is what makes a ragged array
		// render with its columns in a stable, document-ordered sequence rather than an alphabetical one.

		QStringList     keys;
		QSet<QString>   seen;
		const int       elementCount = arrayNode->array_size ();

		for ( int elementIndex = 0; elementIndex < elementCount; ++elementIndex )
		{
			const JsonNode* const element = arrayNode->array_element ( elementIndex );

			for ( int memberIndex = 0; memberIndex < element->member_count (); ++memberIndex )
			{
				const QString& key = element->member_key ( memberIndex );

				if ( !seen.contains ( key ) )
				{
					seen.insert ( key );

					keys.append ( key );
				}
			}
		}

		return keys;
	}

	bool JsonTableModel::derive_object_mode () const
	{
		const int elementCount = arrayNode->array_size ();

		// An EMPTY array is single-value mode: there is no key union to derive, and EDITOR-12's first typed element
		// decides the shape. Committing an object into it re-projects the table on the next present.

		if ( elementCount == 0 )
		{
			return false;
		}

		for ( int elementIndex = 0; elementIndex < elementCount; ++elementIndex )
		{
			if ( arrayNode->array_element ( elementIndex )->kind () != JsonKind::Object )
			{
				return false;
			}
		}

		return true;
	}

	bool JsonTableModel::resync_columns ()
	{
		const QStringList desired = derive_columns ();

		if ( desired == columnKeys )
		{
			return true;
		}

		// -- Remove columns that are gone, backwards in contiguous runs so an erase never shifts a slot still to be
		//    examined (the same shape as JsonTreeModel's removal pass).

		const QSet<QString> desiredKeys ( desired.begin (), desired.end () );

		auto is_removed = [ this, &desiredKeys ] ( int slot )
		{
			return !desiredKeys.contains ( columnKeys.at ( slot ) );
		};

		int slot = static_cast<int> ( columnKeys.size () ) - 1;

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

			beginRemoveColumns ( QModelIndex (), runStart, runEnd );

			columnKeys.erase ( columnKeys.begin () + runStart, columnKeys.begin () + runEnd + 1 );

			endRemoveColumns ();
		}

		// -- Insert the new ones at the position the desired order puts them in.

		for ( int desiredIndex = 0; desiredIndex < desired.size (); ++desiredIndex )
		{
			const QString& key = desired.at ( desiredIndex );

			if ( ( desiredIndex < columnKeys.size () ) && ( columnKeys.at ( desiredIndex ) == key ) )
			{
				continue;
			}

			if ( columnKeys.contains ( key ) )
			{
				// The key survived but moved. A column REORDER cannot be expressed without moving cell data around
				// under the view's current index, so hand back to the caller for a reset rather than emit a lie.

				return false;
			}

			beginInsertColumns ( QModelIndex (), desiredIndex, desiredIndex );

			columnKeys.insert ( desiredIndex, key );

			endInsertColumns ();
		}

		return columnKeys == desired;
	}

	void JsonTableModel::resync_rows ()
	{
		// Rows are diffed by node ADDRESS -- see the header. The shadow's pointers are compared and never dereferenced:
		// after a removal they name destroyed nodes.

		const int elementCount = arrayNode->array_size ();

		// -- Pass 1: remove shadow rows whose element is gone, backwards in contiguous runs.

		QSet<const JsonNode*> liveElements;

		for ( int elementIndex = 0; elementIndex < elementCount; ++elementIndex )
		{
			liveElements.insert ( arrayNode->array_element ( elementIndex ) );
		}

		auto is_removed = [ this, &liveElements ] ( int slot )
		{
			return !liveElements.contains ( rowNodes [ static_cast<size_t> ( slot ) ] );
		};

		int slot = static_cast<int> ( rowNodes.size () ) - 1;

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

			beginRemoveRows ( QModelIndex (), runStart, runEnd );

			rowNodes.erase ( rowNodes.begin () + runStart, rowNodes.begin () + runEnd + 1 );

			endRemoveRows ();
		}

		// -- Pass 2: insert rows for elements the shadow does not hold, again in contiguous runs.

		QSet<const JsonNode*> shadowElements;

		for ( JsonNode* const rowNode : rowNodes )
		{
			shadowElements.insert ( rowNode );
		}

		int shadowCursor = 0;
		int elementIndex = 0;

		while ( elementIndex < elementCount )
		{
			if ( shadowElements.contains ( arrayNode->array_element ( elementIndex ) ) )
			{
				++elementIndex;
				++shadowCursor;

				continue;
			}

			const int runStart = elementIndex;

			while ( ( elementIndex < elementCount ) &&
			        !shadowElements.contains ( arrayNode->array_element ( elementIndex ) ) )
			{
				++elementIndex;
			}

			const int runLength = elementIndex - runStart;

			beginInsertRows ( QModelIndex (), shadowCursor, shadowCursor + runLength - 1 );

			for ( int offset = 0; offset < runLength; ++offset )
			{
				rowNodes.insert
				(
					rowNodes.begin () + shadowCursor + offset,
					arrayNode->array_element ( runStart + offset )
				);
			}

			endInsertRows ();

			shadowCursor += runLength;
		}

		// -- Pass 3: reorder. Both lists now hold the same elements, so a selection sort over the out-of-place runs
		//            settles it -- and a MOVE is one operation rather than a remove plus an insert, which is what lets
		//            the view carry the moved row's selection with it.

		for ( int target = 0; target < elementCount; ++target )
		{
			JsonNode* const wanted = arrayNode->array_element ( target );

			if ( rowNodes [ static_cast<size_t> ( target ) ] == wanted )
			{
				continue;
			}

			int source = -1;

			for ( int search = target + 1; search < static_cast<int> ( rowNodes.size () ); ++search )
			{
				if ( rowNodes [ static_cast<size_t> ( search ) ] == wanted )
				{
					source = search;

					break;
				}
			}

			if ( source < 0 )
			{
				continue;
			}

			beginMoveRows ( QModelIndex (), source, source, QModelIndex (), target );

			std::rotate
			(
				rowNodes.begin () + target,
				rowNodes.begin () + source,
				rowNodes.begin () + source + 1
			);

			endMoveRows ();
		}
	}

	GridPosition JsonTableModel::enclosing_cell ( const JsonPointer& pointer ) const
	{
		// Walk up until the pointer names a cell. A value edit deep inside a container cell still has to repaint that
		// cell's row -- the "{...}" text does not change, but a caller asking "which cell does this belong to" should
		// get an answer rather than a shrug.

		JsonPointer candidate = pointer;

		while ( candidate.token_count () > arrayPointer.token_count () )
		{
			const GridPosition cell = cell_for_pointer ( candidate );

			if ( cell.is_valid () )
			{
				return cell;
			}

			candidate = candidate.parent ();
		}

		return GridPosition {};
	}

	bool JsonTableModel::covers ( const JsonPointer& pointer ) const
	{
		const int prefixLength = arrayPointer.token_count ();

		if ( pointer.token_count () < prefixLength )
		{
			return false;
		}

		for ( int tokenIndex = 0; tokenIndex < prefixLength; ++tokenIndex )
		{
			if ( pointer.token ( tokenIndex ) != arrayPointer.token ( tokenIndex ) )
			{
				return false;
			}
		}

		return true;
	}

	//=================================================================================================================
	// Helpers -- editing
	//=================================================================================================================

	bool JsonTableModel::commit_cell ( JsonNode* target, const JsonPointer& pointer, const QVariant& value )
	{
		// A missing or null cell has nothing to set in Phase 7. Typed entry into either -- which CREATES the member or
		// retypes the null -- is EDITOR-12's JSON-literal rule and lands with the rest of the table clipboard work.

		if ( !is_editable_cell ( target ) )
		{
			return false;
		}

		EditOutcome outcome = EditOutcome::Rejected;

		switch ( target->kind () )
		{
			case JsonKind::String:
			{
				outcome = undo->set_string ( pointer, value.toString () );

				break;
			}

			case JsonKind::Number:
			{
				// VAL-03. Refusing here (rather than letting UndoController reject it) is what returns false to the
				// view, which is what keeps the editor open on the errored cell instead of silently reverting it.

				const QString token = value.toString ().trimmed ();

				if ( !Validator::is_valid_number ( token ) )
				{
					return false;
				}

				outcome = undo->set_number ( pointer, token );

				break;
			}

			case JsonKind::Boolean:
			{
				// The boolean editor is a two-item combo, so the value arrives either as a real bool or as the literal
				// text the combo displays. Both spellings are accepted so the delegate is free to send either.

				const bool booleanValue = ( value.userType () == QMetaType::Bool )
				                        ? value.toBool ()
				                        : ( value.toString ().compare ( cell_text::BOOLEAN_TRUE, Qt::CaseInsensitive ) == 0 );

				outcome = undo->set_boolean ( pointer, booleanValue );

				break;
			}

			default:
			{
				return false;
			}
		}

		// Unchanged is a success from the view's point of view: the user committed, and the cell holds what they typed.
		// Only a rejection has to keep the editor open.

		return outcome != EditOutcome::Rejected;
	}
}

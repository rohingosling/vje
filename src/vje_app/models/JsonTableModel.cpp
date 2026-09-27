//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include <vje_core/services/column_naming.hpp>

#include "models/cell_presentation.hpp"

#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/CellPasteConverter.hpp>
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

		// The view-only provisional row is one extra trailing row with no document element behind it (EDITOR-12).

		return static_cast<int> ( rowNodes.size () ) + ( provisionalActive ? 1 : 0 );
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
				return cell_display_text ( node, stringDisplay );
			}

			case Qt::EditRole:
			{
				return cell_edit_text ( node, stringDisplay );
			}

			case Qt::ToolTipRole:
			{
				// The column cap (config::form::MAXIMUM_COLUMN_WIDTH) means a long value is routinely elided, so the
				// tooltip is how the whole of it stays reachable without opening an editor.

				return is_editable_cell ( node ) ? cell_display_text ( node, stringDisplay ) : QVariant ();
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

			case cell_roles::ESCAPED_NOTATION:
			{
				// Asked of the model rather than read from a setting by the delegate, for the same reason CONTENT_KIND
				// is: the delegate stays ignorant of which model it drives, and of settings entirely (SET-03).

				return mode_edits_in_escaped_notation ( stringDisplay );
			}

			case cell_roles::CELL_LABEL:
			{
				// The column's own header, which is the label the user can see above the cell (NFR-05) -- the member
				// key in object mode, and the array's own key in single-value mode. Taken from headerData rather than
				// from columnKeys so the two can never name the same column differently.

				return headerData ( index.column (), Qt::Horizontal, Qt::DisplayRole );
			}

			default:
			{
				return QVariant ();
			}
		}
	}

	QVariant JsonTableModel::headerData ( int section, Qt::Orientation orientation, int role ) const
	{
		// A COLUMN NAME IS LEFT ALIGNED, matching the cells beneath it -- which are (JsonTableModel::data's
		// TextAlignmentRole), and a header centred over left-aligned values reads as belonging to no column in
		// particular once the column is wider than its name. Qt's default for a horizontal section is centred.
		//
		// A ROW NUMBER IS RIGHT ALIGNED (2026-09-25), so 9 and 10 line up by their last digit as a spreadsheet's do.
		// Stated here rather than left to Qt, whose default for a VERTICAL header is LEFT aligned -- an earlier comment
		// here said "keeps Qt's centring", which Qt never did, and the numbers shipped left aligned under it.
		// GridHeaderView adds the space that keeps them clear of the column's edge.

		if ( role == Qt::TextAlignmentRole )
		{
			const Qt::Alignment alignment = ( orientation == Qt::Horizontal ) ? ( Qt::AlignLeft  | Qt::AlignVCenter )
			                                                                  : ( Qt::AlignRight | Qt::AlignVCenter );

			return QVariant::fromValue ( static_cast<int> ( alignment ) );
		}

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

	StringDisplay JsonTableModel::string_display () const
	{
		return stringDisplay;
	}

	void JsonTableModel::set_string_display ( StringDisplay mode )
	{
		if ( stringDisplay == mode )
		{
			return;
		}

		stringDisplay = mode;

		// Every cell's TEXT changed and nothing else did -- no row or column moved -- so this is a dataChanged over the
		// whole grid rather than a reset, which would take the column widths, the scroll position and the current cell
		// with it (CC4).

		if ( ( rowCount () > 0 ) && ( columnCount () > 0 ) )
		{
			emit dataChanged ( index ( 0, 0 ), index ( rowCount () - 1, columnCount () - 1 ) );
		}
	}

	Qt::ItemFlags JsonTableModel::flags ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return Qt::NoItemFlags;
		}

		// EVERY cell is selectable, including null, missing, and container cells -- the landability rule (EDITOR-03,
		// grid_navigation.hpp).
		//
		// Editability is now wider than in Phase 7: a scalar edits its value, and a null / missing / provisional cell
		// takes a TYPED ENTRY interpreted as a JSON literal (EDITOR-12) -- so all three open an editor. Only a container
		// stays non-editable, because activating one DRILLS IN rather than editing (EDITOR-05).

		Qt::ItemFlags cellFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

		if ( undo != nullptr )
		{
			const CellContent content = cell_content ( node_for_cell ( index.row (), index.column () ) );

			if ( ( content == CellContent::Scalar ) || ( content == CellContent::Null ) || ( content == CellContent::Missing ) )
			{
				cellFlags |= Qt::ItemIsEditable;
			}
		}

		return cellFlags;
	}

	bool JsonTableModel::setData ( const QModelIndex& index, const QVariant& value, int role )
	{
		if ( !index.isValid () || ( role != Qt::EditRole ) || ( undo == nullptr ) )
		{
			return false;
		}

		const int row    = index.row ();
		const int column = index.column ();

		// EDITOR-12: a commit into the PROVISIONAL row materializes it. That removes the row hosting the still-open
		// editor, so the materialize is deferred off this signal (the controller runs it queued); setData reports the
		// commit accepted so the delegate closes the editor cleanly first.

		if ( is_provisional_row ( row ) )
		{
			emit provisional_commit_pending ( column, value.toString () );

			return true;
		}

		JsonNode* const   target  = node_for_cell ( row, column );
		const CellContent content = cell_content ( target );

		// A scalar keeps the Phase 7 path: set_string / set_number / set_boolean, which gives a single-cell patch and
		// the VAL-03 refusal in place.

		if ( content == CellContent::Scalar )
		{
			return commit_cell ( target, pointer_for_cell ( row, column ), value );
		}

		// A null or missing cell takes a TYPED ENTRY interpreted as a JSON literal (EDITOR-12): a valid JSON number /
		// true / false / null / quoted string commits as that value, anything else as a plain string. A null cell
		// retypes; a missing cell creates the member.

		if ( ( content == CellContent::Null ) || ( content == CellContent::Missing ) )
		{
			std::unique_ptr<JsonNode> typed = typed_entry_value ( value.toString (), stringDisplay );

			// Null means the notation refused it -- a malformed escape. Refusing here is what keeps the editor open on
			// the offending text, exactly as a malformed number does (VAL-03).

			if ( typed == nullptr )
			{
				return false;
			}

			return apply_cell_value ( row, column, std::move ( typed ), QStringLiteral ( "Edit Cell" ) );
		}

		return false;   // A container cell drills in; it never edits (EDITOR-05).
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

		// EDITOR-12: an empty array presents its table with one provisional row already in place, so the first element
		// can be typed or pasted directly. A non-empty array grows one only on a bottom-edge move (the controller).

		provisionalActive = ( arrayNode->array_size () == 0 );

		endResetModel ();
	}

	void JsonTableModel::clear_presentation ()
	{
		if ( arrayNode == nullptr )
		{
			return;
		}

		beginResetModel ();

		arrayPointer      = JsonPointer ();
		arrayNode         = nullptr;
		objectMode        = false;
		provisionalActive = false;

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

	std::optional<QString> JsonTableModel::column_key ( int column ) const
	{
		if ( columnKeys.isEmpty () || ( column < 0 ) || ( column >= columnKeys.size () ) )
		{
			return std::nullopt;
		}

		return columnKeys.at ( column );
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

	QString JsonTableModel::column_name ( int column ) const
	{
		const std::optional<QString> key = column_key ( column );

		if ( key.has_value () )
		{
			return key.value ();
		}

		// Single-value mode: the column has no member key, but the ARRAY has a name and that is what the column is
		// called (section 2.12, EDIT-16). Reused from column_naming rather than derived here, so the CSV header and
		// the clipboard's column name are the same string by construction.

		return ( arrayNode != nullptr ) ? column_naming::single_column_name ( *arrayNode ) : column_naming::UNNAMED;
	}

	const JsonNode* JsonTableModel::presented_array () const
	{
		return arrayNode;
	}

	JsonPointer JsonTableModel::element_pointer ( int row ) const
	{
		// Bounded by the DOCUMENT-backed rows rather than by rowCount (), so the provisional row -- which is not an
		// element and has no pointer -- answers the root rather than a position one past the end (EDITOR-12).

		if ( ( row < 0 ) || ( row >= element_count () ) )
		{
			return JsonPointer ();
		}

		return arrayPointer.child ( QString::number ( row ) );
	}

	JsonNode* JsonTableModel::element_node ( int row ) const
	{
		if ( ( arrayNode == nullptr ) || ( row < 0 ) || ( row >= element_count () ) )
		{
			return nullptr;
		}

		return arrayNode->array_element ( row );
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

			emit presented_array_changed ();

			return;
		}

		if ( resolved != arrayNode )
		{
			arrayNode = resolved;

			rebuild ();

			emit presented_array_changed ();

			return;
		}

		// covers() asks whether the change was AT or INSIDE the projected array. A collapsed change batch names the
		// deepest common ancestor of everything it touched, which for a column gesture is the array itself but for a
		// wider group can be above it -- and a change above may have changed anything below. So an ancestor counts.

		const bool isAncestor = ( pointer.token_count () < arrayPointer.token_count () )
		                     && ( JsonPointer::common_ancestor ( arrayPointer, pointer ) == pointer );

		if ( !covers ( pointer ) && !isAncestor )
		{
			return;
		}

		// Everything past this point is a real change to the projected array, which is exactly what EDIT-15's sort
		// marker must not outlive.

		emit presented_array_changed ();

		// UNDO-05: a change at or inside ONE element says which row it was. The document names the deepest node a
		// change (or a whole batch of them) touched, so an undo that put back one row's values names that element or a
		// member of it, while one spanning rows names the array and is deliberately not a row. The element index is
		// read before the resync below, which is right for the only case it serves: a change INSIDE an element leaves
		// the element where it was.

		if ( pointer.token_count () > arrayPointer.token_count () )
		{
			bool      isIndex = false;
			const int row     = pointer.token ( arrayPointer.token_count () ).toInt ( &isIndex );

			if ( isIndex && ( row >= 0 ) && ( row < element_count () ) )
			{
				emit element_changed ( row );
			}
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

		// A rebuild is a fresh projection, so the empty-array provisional invariant is re-established and any grown
		// provisional row is dropped (materializing the first element of an empty array reaches here and must not leave
		// a second provisional behind).

		provisionalActive = ( arrayNode->array_size () == 0 );

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
				// See JsonFormModel: the editor's text is in the SET-03 notation and is read back through the inverse
				// of the rule that produced it, refusing rather than committing a malformed escape.

				QString committed;

				if ( !string_commit_value ( value.toString (), stringDisplay, committed ) )
				{
					return false;
				}

				outcome = undo->set_string ( pointer, committed );

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

	//=================================================================================================================
	// Provisional row (EDITOR-12)
	//=================================================================================================================

	int JsonTableModel::element_count () const
	{
		return static_cast<int> ( rowNodes.size () );
	}

	void JsonTableModel::set_provisional_row ( bool active )
	{
		if ( ( active == provisionalActive ) || ( arrayNode == nullptr ) )
		{
			return;
		}

		// The provisional row is always the trailing one, at the index just past the real rows.

		const int row = static_cast<int> ( rowNodes.size () );

		if ( active )
		{
			beginInsertRows ( QModelIndex (), row, row );
			provisionalActive = true;
			endInsertRows ();
		}
		else
		{
			beginRemoveRows ( QModelIndex (), row, row );
			provisionalActive = false;
			endRemoveRows ();
		}
	}

	bool JsonTableModel::has_provisional_row () const
	{
		return provisionalActive;
	}

	bool JsonTableModel::is_provisional_row ( int row ) const
	{
		return provisionalActive && ( row == static_cast<int> ( rowNodes.size () ) );
	}

	bool JsonTableModel::materialize_provisional ( int column, std::unique_ptr<JsonNode> value )
	{
		if ( !provisionalActive || ( undo == nullptr ) || ( arrayNode == nullptr ) )
		{
			return false;
		}

		// Drop the view-only row first, then append the real element. The append's node_changed drives resync, which
		// inserts the real row at the same index -- so the provisional row is swapped for the real one and column widths,
		// scroll, and (restored by the controller) the current cell survive (EDITOR-12).

		set_provisional_row ( false );

		std::unique_ptr<JsonNode> element = build_new_element ( column, std::move ( value ) );

		return undo->append_element ( arrayPointer, std::move ( element ), QStringLiteral ( "Add Element" ) ) != EditOutcome::Rejected;
	}

	std::unique_ptr<JsonNode> JsonTableModel::build_new_element ( int column, std::unique_ptr<JsonNode> value ) const
	{
		// Single-value table (a scalar array, or an empty array whose first element decides the shape): the element IS the
		// committed value. Committing an object into an empty array re-projects to a multi-column table on the next resync.

		if ( !objectMode )
		{
			return value;
		}

		// Object table: an object carrying the committed member plus null for every other column, in column order.

		std::unique_ptr<JsonNode> element = JsonNode::make_object ();

		for ( int columnIndex = 0; columnIndex < columnKeys.size (); ++columnIndex )
		{
			element->append_member
			(
				columnKeys.at ( columnIndex ),
				( columnIndex == column ) ? std::move ( value ) : JsonNode::make_null ()
			);
		}

		return element;
	}

	//=================================================================================================================
	// Cell value application (EDITOR-11 paste, EDITOR-12 typed entry)
	//=================================================================================================================

	bool JsonTableModel::apply_cell_value ( int row, int column, std::unique_ptr<JsonNode> value, const QString& text )
	{
		if ( ( undo == nullptr ) || ( value == nullptr ) )
		{
			return false;
		}

		if ( is_provisional_row ( row ) )
		{
			return materialize_provisional ( column, std::move ( value ) );
		}

		JsonNode* const target = node_for_cell ( row, column );

		// A missing (ragged) cell CREATES the member; any existing cell -- scalar, null, or container -- has its value
		// replaced wholesale. Both are one undoable step.

		if ( cell_content ( target ) == CellContent::Missing )
		{
			return create_missing_member ( row, column, std::move ( value ), text );
		}

		return undo->replace_subtree ( pointer_for_cell ( row, column ), std::move ( value ), text ) != EditOutcome::Rejected;
	}

	EditOutcome JsonTableModel::sort_by_column ( int column, Qt::SortOrder order )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		// In single-value mode the one column IS the elements, which json_order names with nullopt; in object mode the
		// column names a member. A column outside the projection names nothing and is refused rather than silently
		// falling back to sorting by element, which would order the array by a rule the user never asked for.

		if ( objectMode && !column_key ( column ).has_value () )
		{
			return EditOutcome::Rejected;
		}

		if ( !objectMode && ( column != 0 ) )
		{
			return EditOutcome::Rejected;
		}

		return undo->sort_array ( arrayPointer, column_key ( column ), order );
	}

	int JsonTableModel::apply_cell_values ( std::vector<CellAssignment>& assignments, const QString& text )
	{
		if ( ( undo == nullptr ) || assignments.empty () )
		{
			return 0;
		}

		// Lazily opened, so a run in which every assignment turned out to be refused leaves no undo step that undoes
		// nothing (lesson D19 / Q43). One group whatever the run touched: EDITOR-18's "one undo step each".

		UndoController::MacroScope macro ( *undo, text );

		int applied = 0;

		for ( CellAssignment& assignment : assignments )
		{
			// Each assignment names a distinct cell, and no cell's pointer depends on another's having been applied --
			// an element's index is fixed for the run, and a member's pointer is its KEY, which creating or replacing
			// a sibling member cannot move. So this runs forward, unlike EDIT-14's removals.

			if ( apply_cell_value ( assignment.row, assignment.column, std::move ( assignment.value ), text ) )
			{
				++applied;
			}
		}

		return applied;
	}

	EditOutcome JsonTableModel::append_pasted_element ( std::vector<PastedMember>& members )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) || members.empty () )
		{
			return EditOutcome::Rejected;
		}

		// A KEYLESS single member is a scalar array's growth: the value IS the element, since there is no name to put
		// it under and inventing one would change the array's kind.

		if ( ( members.size () == 1 ) && members.front ().key.isNull () )
		{
			return undo->append_element
			(
				arrayPointer, std::move ( members.front ().value ), QStringLiteral ( "Paste" )
			);
		}

		std::unique_ptr<JsonNode> element = JsonNode::make_object ();

		// EVERY column the table already has, in its own order, filled with null -- then the pasted members written
		// over the ones they name. Doing it in this order rather than appending the pasted members afterwards is what
		// keeps the new element's member order the same as every other element's, so the table does not gain a column
		// at the end that is really one it already had.

		for ( const QString& columnKey : columnKeys )
		{
			element->append_member ( columnKey, JsonNode::make_null () );
		}

		for ( PastedMember& member : members )
		{
			if ( member.key.isNull () || ( member.value == nullptr ) )
			{
				continue;
			}

			if ( JsonNode* const existing = element->find_member ( member.key ) )
			{
				element->replace_member_value ( existing->index_in_parent (), std::move ( member.value ) );
			}
			else
			{
				element->append_member ( member.key, std::move ( member.value ) );
			}
		}

		return undo->append_element ( arrayPointer, std::move ( element ), QStringLiteral ( "Paste" ) );
	}

	std::unique_ptr<JsonNode> JsonTableModel::build_pasted_element
	(
		const std::vector<PastedMember>& members,
		bool                             keyed,
		QStringList*                     newKeys
	) const
	{
		// A SINGLE-VALUE SOURCE onto a single-value table: its one cell IS its element, so the element is that value
		// bare. Onto an array of objects the same cell is written BY KEY like any other (its key is the name its array
		// carried, section 2.12), which is the rule the user chose for a row and the only name the value has.

		if ( !objectMode && !keyed )
		{
			for ( const PastedMember& member : members )
			{
				if ( member.value != nullptr )
				{
					return member.value->clone ();
				}
			}

			return nullptr;
		}

		std::unique_ptr<JsonNode> element = JsonNode::make_object ();

		// An array of objects: the table's own columns first, in the table's order, so the new element reads like its
		// neighbours and a column it lacks is null rather than absent -- the fill EDIT-11 would apply, done at the
		// moment the element is made. A single-value table has no columns to fill, so there the element is exactly the
		// members the row carried.

		if ( objectMode )
		{
			for ( const QString& columnKey : columnKeys )
			{
				element->append_member ( columnKey, JsonNode::make_null () );
			}
		}

		for ( const PastedMember& member : members )
		{
			// An ABSENT cell names a member the source element lacked, so it contributes nothing -- and a nameless one
			// (a payload older than the keyed flag) has no member to be.

			if ( ( member.value == nullptr ) || member.key.isNull () )
			{
				continue;
			}

			if ( JsonNode* const existing = element->find_member ( member.key ) )
			{
				element->replace_member_value ( existing->index_in_parent (), member.value->clone () );

				continue;
			}

			element->append_member ( member.key, member.value->clone () );

			if ( objectMode && ( newKeys != nullptr ) )
			{
				newKeys->append ( member.key );
			}
		}

		return element;
	}

	EditOutcome JsonTableModel::insert_pasted_element ( int row, std::unique_ptr<JsonNode> element, const QStringList& newKeys )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) || ( element == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		const int position = std::clamp ( row, 0, element_count () );

		UndoController::MacroScope macro ( *undo, QStringLiteral ( "Paste Row" ) );

		// The view-only row goes BEFORE the array changes under it -- materialize_provisional's order, and for the same
		// reason: the insert's own notification then re-projects a table with no stale trailing row to account for.

		set_provisional_row ( false );

		if ( undo->insert_element_at ( arrayPointer, position, std::move ( element ), QStringLiteral ( "Paste Row" ) )
		     == EditOutcome::Rejected )
		{
			return EditOutcome::Rejected;
		}

		// Every OTHER element gains the columns the row brought, as null, at its end -- create_pasted_member's fill, so
		// a pasted row with a member the table lacked adds a column rather than making the array ragged. Each element
		// is named by its index AFTER the insert, which is why the inserted one is skipped by position.

		for ( int other = 0; other < arrayNode->array_size (); ++other )
		{
			if ( other == position )
			{
				continue;
			}

			JsonNode* const target = arrayNode->array_element ( other );

			if ( ( target == nullptr ) || ( target->kind () != JsonKind::Object ) )
			{
				continue;
			}

			for ( const QString& key : newKeys )
			{
				if ( target->has_member ( key ) )
				{
					continue;
				}

				undo->insert_member_at
				(
					arrayPointer.child ( QString::number ( other ) ),
					target->member_count (),
					key,
					JsonNode::make_null (),
					QStringLiteral ( "Paste Row" )
				);
			}
		}

		return macro.pushed () ? EditOutcome::Applied : EditOutcome::Unchanged;
	}

	int JsonTableModel::column_index ( const QString& key ) const
	{
		return objectMode ? static_cast<int> ( columnKeys.indexOf ( key ) ) : -1;
	}

	EditOutcome JsonTableModel::create_pasted_member ( int row, const QString& key, std::unique_ptr<JsonNode> value )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) || key.isNull () || ( value == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		if ( ( row < 0 ) || ( row >= element_count () ) )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* const target = arrayNode->array_element ( row );

		if ( ( target == nullptr ) || ( target->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		// The value first, then `null` on every other element. A column a paste introduces has to exist across the
		// whole array or the paste has quietly made it ragged -- which is the same fill EDIT-11's Normalize applies,
		// done here at the moment the column appears rather than left for the user to repair afterwards.
		//
		// The order matters only for readability: each insertion names its element by pointer, and an object member's
		// pointer is its key, so nothing here renumbers anything (delete_column's note, from the other direction).

		const EditOutcome outcome = undo->insert_member_at
		(
			arrayPointer.child ( QString::number ( row ) ),
			target->member_count (),
			key,
			std::move ( value ),
			QStringLiteral ( "Paste" )
		);

		if ( outcome == EditOutcome::Rejected )
		{
			return outcome;
		}

		for ( int otherRow = 0; otherRow < element_count (); ++otherRow )
		{
			if ( otherRow == row )
			{
				continue;
			}

			JsonNode* const element = arrayNode->array_element ( otherRow );

			if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) || element->has_member ( key ) )
			{
				continue;
			}

			undo->insert_member_at
			(
				arrayPointer.child ( QString::number ( otherRow ) ),
				element->member_count (),
				key,
				JsonNode::make_null (),
				QStringLiteral ( "Paste" )
			);
		}

		return EditOutcome::Applied;
	}

	UndoController* JsonTableModel::undo_controller () const
	{
		return undo;
	}

	EditOutcome JsonTableModel::delete_row ( int row )
	{
		if ( undo == nullptr )
		{
			return EditOutcome::Rejected;
		}

		const JsonPointer target = element_pointer ( row );

		// A root pointer means the row is not an element -- the provisional row (EDITOR-12), or out of range.

		if ( target.is_root () )
		{
			return EditOutcome::Rejected;
		}

		return undo->delete_node ( target );
	}

	EditOutcome JsonTableModel::delete_column ( int column )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		const std::optional<QString> key = column_key ( column );

		// A single-value table's one column IS the elements, so there is no member to remove and the command is
		// refused rather than quietly emptying the array (EDITOR-18).

		if ( !key.has_value () )
		{
			return EditOutcome::Rejected;
		}

		// The pointers are gathered BEFORE any removal, which is safe here for the reason apply_cell_values runs
		// forward: an object member's pointer is its key, so removing "k" from element 0 moves nothing that element 1's
		// pointer depends on, and removing it from an element does not renumber the elements themselves. This is the
		// one place in the application where a multi-node removal does NOT need EDIT-14's descending order, and it is
		// worth saying why rather than leaving the difference to be noticed.
		//
		// has_member BOUNDS THE WORK, NOT THE ANSWER, and says so because a neutered build proved it: without it,
		// every element contributes a pointer, the ones that resolve to nothing are Rejected by delete_node, and the
		// outcome is identical in both branches -- Applied where any member existed, Unchanged where none did. What
		// it saves is a pointer built and a resolve attempted per element that never had the member, which on a large
		// ragged array is the whole array. Lesson D25's shape: a guard invisible to every result-comparing case.

		QList<JsonPointer> targets;

		for ( int row = 0; row < arrayNode->array_size (); ++row )
		{
			const JsonNode* const element = arrayNode->array_element ( row );

			if ( ( element != nullptr ) && ( element->kind () == JsonKind::Object ) && element->has_member ( key.value () ) )
			{
				targets.append ( arrayPointer.child ( QString::number ( row ) ).child ( key.value () ) );
			}
		}

		if ( targets.isEmpty () )
		{
			return EditOutcome::Unchanged;
		}

		UndoController::MacroScope macro ( *undo, QStringLiteral ( "Delete Column" ) );

		for ( const JsonPointer& target : targets )
		{
			undo->delete_node ( target );
		}

		return macro.pushed () ? EditOutcome::Applied : EditOutcome::Unchanged;
	}

	EditOutcome JsonTableModel::clear_column ( int column )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) || ( column < 0 ) || ( column >= columnCount () ) )
		{
			return EditOutcome::Rejected;
		}

		// element_count () rather than rowCount (): EDITOR-12's provisional row is not an element and has no cell to
		// clear -- the same bound selected_cell_positions uses, for the same reason.
		//
		// A missing cell and an already-null one are both SKIPPED, which is what makes the outcome honest: a column of
		// nulls reports Unchanged rather than pushing an undo step that undoes nothing (D19 / Q43), and a ragged
		// array comes out exactly as ragged as it went in.

		std::vector<CellAssignment> assignments;

		for ( int row = 0; row < element_count (); ++row )
		{
			const JsonNode* const cell = grid_node ( row, column );

			if ( ( cell == nullptr ) || ( cell->kind () == JsonKind::Null ) )
			{
				continue;
			}

			assignments.push_back ( { row, column, JsonNode::make_null () } );
		}

		if ( assignments.empty () )
		{
			return EditOutcome::Unchanged;
		}

		// apply_cell_values opens the one MacroScope and runs FORWARD, which is right here for its own stated reason:
		// no cell's pointer depends on another's having been applied. This is the column-shaped counterpart of
		// delete_column's note -- neither needs EDIT-14's descending order, and both say why.

		return ( apply_cell_values ( assignments, QStringLiteral ( "Clear Column" ) ) > 0 )
		     ? EditOutcome::Applied
		     : EditOutcome::Unchanged;
	}

	EditOutcome JsonTableModel::clear_row ( int row )
	{
		// element_count () rather than rowCount (), for clear_column's reason: the provisional row is not an element.

		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) || ( row < 0 ) || ( row >= element_count () ) )
		{
			return EditOutcome::Rejected;
		}

		// grid_node answers per CELL, so this one loop serves both table modes: in object mode each column is a member
		// of the element and a missing one answers nullptr, and in single-value mode the one column IS the element.
		// Skipping the missing and the already-null is what keeps the outcome honest, exactly as it does for a column.

		std::vector<CellAssignment> assignments;

		for ( int column = 0; column < columnCount (); ++column )
		{
			const JsonNode* const cell = grid_node ( row, column );

			if ( ( cell == nullptr ) || ( cell->kind () == JsonKind::Null ) )
			{
				continue;
			}

			assignments.push_back ( { row, column, JsonNode::make_null () } );
		}

		if ( assignments.empty () )
		{
			return EditOutcome::Unchanged;
		}

		// Forward, inside apply_cell_values' one MacroScope: every assignment names a member of the SAME element by its
		// key, so no cell's pointer depends on another's having been written.

		return ( apply_cell_values ( assignments, QStringLiteral ( "Clear Row" ) ) > 0 )
		     ? EditOutcome::Applied
		     : EditOutcome::Unchanged;
	}

	EditOutcome JsonTableModel::rename_column ( int column, const QString& newKey )
	{
		if ( ( undo == nullptr ) || ( arrayNode == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		const std::optional<QString> key = column_key ( column );

		// A single-value table's one column IS the elements, so there is no member to rename -- delete_column's
		// refusal, for the same reason and in the same place.

		if ( !key.has_value () )
		{
			return EditOutcome::Rejected;
		}

		if ( key.value () == newKey )
		{
			return EditOutcome::Unchanged;
		}

		// PLANNED BEFORE APPLIED. Two passes over the elements: the first decides, the second edits. A rename that
		// discovered a collision half way through would leave the array carrying both names under one undo step.

		QList<JsonPointer> targets;

		for ( int row = 0; row < arrayNode->array_size (); ++row )
		{
			const JsonNode* const element = arrayNode->array_element ( row );

			if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) || !element->has_member ( key.value () ) )
			{
				continue;
			}

			// VAL-02, asked per element because a ragged array can carry the new name on some and not others. The
			// policy is UndoController's (SET-03a), so it is asked rather than restated -- a build that let this
			// through would simply be refused element by element below, which is the half-applied state this pass
			// exists to prevent.

			if ( !undo->allow_duplicate_keys () && element->has_member ( newKey ) )
			{
				return EditOutcome::Rejected;
			}

			targets.append ( arrayPointer.child ( QString::number ( row ) ).child ( key.value () ) );
		}

		if ( targets.isEmpty () )
		{
			return EditOutcome::Unchanged;
		}

		UndoController::MacroScope macro ( *undo, QStringLiteral ( "Rename Column" ) );

		for ( const JsonPointer& target : targets )
		{
			undo->rename_key ( target, newKey );
		}

		return macro.pushed () ? EditOutcome::Applied : EditOutcome::Unchanged;
	}

	bool JsonTableModel::create_missing_member ( int row, int column, std::unique_ptr<JsonNode> value, const QString& text )
	{
		const bool rowInRange = ( row >= 0 ) && ( row < static_cast<int> ( rowNodes.size () ) );

		if ( !rowInRange || !objectMode || ( column < 0 ) || ( column >= columnKeys.size () ) )
		{
			return false;
		}

		JsonNode* const element = rowNodes [ static_cast<size_t> ( row ) ];

		if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
		{
			return false;
		}

		// Insert after the nearest preceding column the element already has, so the new member lands where the table's
		// column order implies rather than at the end (EDITOR-11).

		int insertIndex = 0;

		for ( int precedingColumn = column - 1; precedingColumn >= 0; --precedingColumn )
		{
			JsonNode* const precedingMember = element->find_member ( columnKeys.at ( precedingColumn ) );

			if ( precedingMember != nullptr )
			{
				insertIndex = precedingMember->index_in_parent () + 1;

				break;
			}
		}

		const JsonPointer elementPointer = arrayPointer.child ( QString::number ( row ) );

		return undo->insert_member_at
		(
			elementPointer, insertIndex, columnKeys.at ( column ), std::move ( value ), text
		) != EditOutcome::Rejected;
	}
}

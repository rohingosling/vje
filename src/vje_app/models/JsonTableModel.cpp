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

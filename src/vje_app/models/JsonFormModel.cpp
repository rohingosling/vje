//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonFormModel implementation. See the header for the form/table parity argument and the duplicate-key rule.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonFormModel.hpp"

#include "models/cell_presentation.hpp"

#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/Validator.hpp>

#include <QSet>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonFormModel::JsonFormModel ( JsonDocument* document, UndoController* undo, QObject* parent )
		: QAbstractTableModel ( parent )
		, document ( document )
		, undo     ( undo )
	{
		connect ( document, &JsonDocument::reset,        this, &JsonFormModel::handle_document_reset );
		connect ( document, &JsonDocument::node_changed, this, &JsonFormModel::handle_node_changed );
	}

	//=================================================================================================================
	// QAbstractTableModel
	//=================================================================================================================

	int JsonFormModel::rowCount ( const QModelIndex& parent ) const
	{
		if ( parent.isValid () )
		{
			return 0;
		}

		return static_cast<int> ( rowNodes.size () ) + ( provisionalActive ? 1 : 0 );
	}

	int JsonFormModel::columnCount ( const QModelIndex& parent ) const
	{
		if ( parent.isValid () )
		{
			return 0;
		}

		// ALWAYS two, presented or not. The form's shape is fixed -- a label column and a value column -- so an empty
		// form is zero ROWS, not zero columns.
		//
		// This is load-bearing, not cosmetic. FormView sets a PER-SECTION resize mode on each column at construction,
		// before anything is presented, and QHeaderView::setSectionResizeMode(int, ResizeMode) resolves the logical
		// index through visualIndex(), which answers -1 when the model has no columns. The Q_ASSERT that would catch
		// that is compiled out of a release Qt, leaving an out-of-bounds read at index -1. It segfaulted on Linux
		// (Qt 6.8) and silently did not on Windows (Qt 6.10).

		return COLUMN_COUNT;
	}

	QVariant JsonFormModel::data ( const QModelIndex& index, int role ) const
	{
		if ( !index.isValid () )
		{
			return QVariant ();
		}

		const int row = index.row ();

		// EDITOR-15: the provisional row is view-only and has no node behind it. It renders EMPTY in both columns --
		// no key, no "null" placeholder -- because there is nothing there yet, and a placeholder would read as a
		// member that already exists.

		if ( is_provisional_row ( row ) )
		{
			const bool isKeyCell = ( index.column () == KEY_COLUMN );

			switch ( role )
			{
				case Qt::DisplayRole:
				{
					// THE VALUE CELL EXPLAINS ITSELF RATHER THAN SITTING BLANK (2026-08-03, found by manual smoke).
					// There is no member yet, so there is no value -- the cell is an artifact of the grid having two
					// columns, not a thing with a referent behind it. Left blank it looked fillable, and typing into
					// it did nothing at all, which is the silent refusal VAL-05 exists to forbid.
					//
					// It is NOT a placeholder for an absent value the way "null" is; it is an instruction. But it
					// borrows exactly that presentation -- see CONTENT_KIND below -- because "a cell you can see and
					// cannot edit" is a rule the form already has, and inventing a second dimmed style for it would
					// make two things that mean the same thing look different.

					return isKeyCell ? QString () : tr ( "enter a key first" );
				}

				case cell_roles::CONTENT_KIND:
				{
					// Null is what makes the delegate render it dimmed and read-only. The KEY cell is an ordinary
					// scalar, because it is the one cell here that genuinely edits.

					return static_cast<int> ( isKeyCell ? CellContent::Scalar : CellContent::Null );
				}

				case cell_roles::VALUE_KIND:   return static_cast<int> ( JsonKind::String );
				case cell_roles::IS_KEY_CELL:  return isKeyCell;
				case cell_roles::RIVAL_KEYS:   return rowKeys_as_rivals ();
				case cell_roles::CELL_LABEL:   return isKeyCell ? tr ( "New key" ) : tr ( "New value" );
				default:                       return QVariant ();
			}
		}

		// -- The key label. A plain string whatever the value beside it is, so the shared delegate renders it as
		//    ordinary text and never as a drill-in or null placeholder.

		if ( index.column () == KEY_COLUMN )
		{
			switch ( role )
			{
				case Qt::DisplayRole:
				case Qt::ToolTipRole:
				case Qt::EditRole:
				{
					// EditRole matters as much as DisplayRole now that a key can be renamed in place (EDIT-02): without
					// it the editor would open on an empty field and a stray Enter would blank the key.

					return key_for_row ( row );
				}

				case cell_roles::IS_KEY_CELL:
				{
					return true;
				}

				case cell_roles::RIVAL_KEYS:
				{
					return rival_keys_for_row ( row );
				}

				case cell_roles::CELL_LABEL:
				{
					// A key cell is not named after its own text: that text IS the thing being edited, so announcing
					// it would say the old key while the user replaces it. What the cell is, is the key column
					// (NFR-05).

					return tr ( "Key" );
				}

				case Qt::TextAlignmentRole:
				{
					return QVariant::fromValue ( static_cast<int> ( Qt::AlignLeft | Qt::AlignVCenter ) );
				}

				case cell_roles::CONTENT_KIND:
				{
					return static_cast<int> ( CellContent::Scalar );
				}

				case cell_roles::VALUE_KIND:
				{
					return static_cast<int> ( JsonKind::String );
				}

				default:
				{
					return QVariant ();
				}
			}
		}

		JsonNode* const node = value_node ( row );

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
				// A VALUE cell is named after the key beside it -- the label the user can see against that row, which
				// is exactly what a form field's name should be (NFR-05).

				return key_for_row ( row );
			}

			default:
			{
				return QVariant ();
			}
		}
	}

	void JsonFormModel::set_string_display ( StringDisplay mode )
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

	Qt::ItemFlags JsonFormModel::flags ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return Qt::NoItemFlags;
		}

		// Both columns are landable: Up / Down move the highlight over every field, and the key column is where the
		// EDITOR-02 right-click acts. Only the value column of an editable scalar opens an editor.

		Qt::ItemFlags fieldFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

		// EDITOR-15: only the provisional row's KEY opens an editor, and only while key editing is allowed (SET-05a).
		// Its value cell is landable but not editable -- there is no member yet to give a value to, and the key is
		// what brings one into existence.

		if ( is_provisional_row ( index.row () ) )
		{
			if ( ( index.column () == KEY_COLUMN ) && is_key_editing_allowed () && ( undo != nullptr ) )
			{
				fieldFlags |= Qt::ItemIsEditable;
			}

			return fieldFlags;
		}

		const bool isEditable = ( index.column () == VALUE_COLUMN ) ? is_editable_row   ( index.row () )
		                                                           : is_renameable_row ( index.row () );

		if ( isEditable )
		{
			fieldFlags |= Qt::ItemIsEditable;
		}

		return fieldFlags;
	}

	bool JsonFormModel::is_editable_row ( int row ) const
	{
		// The single editability rule, asked by BOTH flags() and the commit path. Keeping them apart is how a guard
		// ends up decorating the UI while the write path walks straight past it.

		if ( undo == nullptr )
		{
			return false;
		}

		// EDITOR-13 (Phase 15, closing OQ-2): a NULL member is editable too, and takes EDITOR-12's typed entry. Until
		// then this asked is_editable_cell, which answers Scalar alone -- so a null field was a dimmed read-only
		// placeholder and the Form View could not give it a value at all. The array table has answered otherwise since
		// Phase 9 (JsonTableModel::flags); this is the same question, finally answered the same way.
		//
		// A CONTAINER still is not editable: activating one drills in (EDITOR-05), which is a different gesture.

		const CellContent content = cell_content ( value_node ( row ) );

		if ( ( content != CellContent::Scalar ) && ( content != CellContent::Null ) )
		{
			return false;
		}

		// THE DUPLICATE-KEY GUARD IS GONE (2026-08-28), and it is worth saying what it was rather than deleting it
		// silently. A duplicate member was read-only -- value and key alike -- because a JSON Pointer named the FIRST
		// member with a key, so a commit against the second would have written the first, silently, under a
		// correct-looking undo entry. That was a correctness problem and never a permission one.
		//
		// pointer_for_row now records WHICH member it means (15h.5), so the commit lands where the user is looking.
		// The rule therefore lifts for every document rather than answering to SET-03a: a file that ARRIVED with
		// duplicates was never the user's doing, and leaving it uneditable while a file they created duplicates in was
		// editable would be the setting deciding something it has no view on.

		return true;
	}

	bool JsonFormModel::setData ( const QModelIndex& index, const QVariant& value, int role )
	{
		const bool isCommit = index.isValid () && ( role == Qt::EditRole ) && ( undo != nullptr );

		if ( !isCommit )
		{
			return false;
		}

		// EDITOR-15: a commit into the PROVISIONAL row's key materializes it, which removes the row hosting the still
		// open editor -- so the work is deferred off this signal, exactly as the array table defers its own. setData
		// reports the commit accepted so the delegate closes the editor cleanly first.

		if ( is_provisional_row ( index.row () ) )
		{
			if ( index.column () != KEY_COLUMN )
			{
				return false;
			}

			emit provisional_key_committed ( value.toString () );

			return true;
		}

		return ( index.column () == KEY_COLUMN ) ? rename_row ( index.row (), value.toString () )
		                                         : commit_row ( index.row (), value );
	}

	//=================================================================================================================
	// Presentation
	//=================================================================================================================

	void JsonFormModel::present ( const JsonPointer& pointer )
	{
		JsonNode* const resolved = document->resolve ( pointer );

		// An object, or a scalar ROOT (the one node with no parent form to fall back on, EDITOR-02). An array belongs
		// to JsonTableModel and a non-root scalar is presented through its parent, so both are refused here.

		const bool isObject     = ( resolved != nullptr ) && ( resolved->kind () == JsonKind::Object );
		const bool isScalarRoot = ( resolved != nullptr ) && resolved->is_scalar () && pointer.is_root ();

		if ( !isObject && !isScalarRoot )
		{
			clear_presentation ();

			return;
		}

		beginResetModel ();

		objectPointer = pointer;
		objectNode    = resolved;

		capture_rows ();

		// EDITOR-15: an EMPTY object presents with a provisional row already in place, so the first member can be
		// typed straight in -- exactly the rule EDITOR-12 states for an empty array. Set inside the reset rather than
		// through set_provisional_row, whose begin/endInsertRows would be nested inside begin/endResetModel.

		provisionalActive = rowNodes.empty () && !is_scalar_root_form () && is_key_editing_allowed ()
		                 && ( undo != nullptr );

		endResetModel ();
	}

	void JsonFormModel::clear_presentation ()
	{
		if ( objectNode == nullptr )
		{
			return;
		}

		beginResetModel ();

		objectPointer = JsonPointer ();
		objectNode    = nullptr;

		rowNodes.clear ();
		rowKeys .clear ();

		endResetModel ();
	}

	bool JsonFormModel::is_presenting () const
	{
		return objectNode != nullptr;
	}

	const JsonPointer& JsonFormModel::presented_pointer () const
	{
		return objectPointer;
	}

	//=================================================================================================================
	// Policy
	//=================================================================================================================

	void JsonFormModel::set_key_editing_allowed ( bool allowed )
	{
		if ( keyEditingAllowed == allowed )
		{
			return;
		}

		keyEditingAllowed = allowed;

		// Only the key column's FLAGS changed -- no row appeared, moved or went away, and no displayed text differs. A
		// dataChanged over that column is therefore the exact signal: the view re-asks flags() for those cells and
		// nothing else is disturbed (widths, scroll and the current cell all survive, EDITOR-03).

		const int lastRow = rowCount () - 1;

		if ( lastRow >= 0 )
		{
			emit dataChanged ( index ( 0, KEY_COLUMN ), index ( lastRow, KEY_COLUMN ) );
		}
	}

	bool JsonFormModel::is_key_editing_allowed () const
	{
		return keyEditingAllowed;
	}

	void JsonFormModel::set_duplicate_keys_allowed ( bool allowed )
	{
		if ( duplicateKeysAllowed == allowed )
		{
			return;
		}

		duplicateKeysAllowed = allowed;

		// Nothing is re-laid-out: this changes what the key EDITOR will accept, which is asked when one opens. The
		// read-only rule it used to sit beside is gone, so no cell's flags depend on it any more.
	}

	bool JsonFormModel::is_duplicate_keys_allowed () const
	{
		return duplicateKeysAllowed;
	}

	//=================================================================================================================
	// Row Addressing
	//=================================================================================================================

	JsonNode* JsonFormModel::value_node ( int row ) const
	{
		const bool rowInRange = ( row >= 0 ) && ( row < static_cast<int> ( rowNodes.size () ) );

		return rowInRange ? rowNodes [ static_cast<size_t> ( row ) ] : nullptr;
	}

	QString JsonFormModel::key_for_row ( int row ) const
	{
		const bool rowInRange = ( row >= 0 ) && ( row < rowKeys.size () );

		return rowInRange ? rowKeys.at ( row ) : QString ();
	}

	JsonPointer JsonFormModel::pointer_for_row ( int row ) const
	{
		if ( is_scalar_root_form () )
		{
			return objectPointer;
		}

		const bool rowInRange = ( row >= 0 ) && ( row < rowKeys.size () );

		if ( !rowInRange )
		{
			return objectPointer;
		}

		const JsonPointer memberPointer = objectPointer.child ( rowKeys.at ( row ) );

		// WHICH member, where the key is duplicated (15h.5) -- the same disambiguation the tree records, from the same
		// helper, and the reason a duplicate member is editable at all. Recorded only where the key is duplicated, so
		// an ordinary member's pointer is byte for byte what it always was.

		if ( objectNode == nullptr )
		{
			return memberPointer;
		}

		const int occurrence = JsonPointer::occurrence_for ( *objectNode, row );

		return ( occurrence >= 0 ) ? memberPointer.with_occurrence ( objectPointer.token_count (), occurrence )
		                           : memberPointer;
	}

	int JsonFormModel::row_for_pointer ( const JsonPointer& pointer ) const
	{
		if ( objectNode == nullptr )
		{
			return -1;
		}

		if ( is_scalar_root_form () )
		{
			return ( pointer == objectPointer ) ? 0 : -1;
		}

		const bool isDirectChild = covers ( pointer ) &&
		                           ( pointer.token_count () == ( objectPointer.token_count () + 1 ) );

		if ( !isDirectChild )
		{
			return -1;
		}

		const int      tokenIndex = objectPointer.token_count ();
		const QString& token      = pointer.token ( tokenIndex );

		// The recorded position where the pointer carries one, VERIFIED against the key before it is trusted --
		// JsonPointer::resolve's own rule, restated here because this maps the other way and cannot call it. A
		// pointer without one falls through to the first match, which is what every parsed pointer gets and what
		// this always did.

		const int occurrence = pointer.occurrence ( tokenIndex );

		const bool positionHolds = ( occurrence >= 0 )
		                        && ( occurrence < rowKeys.size () )
		                        && ( rowKeys.at ( occurrence ) == token );

		return positionHolds ? occurrence : rowKeys.indexOf ( token );
	}

	//=================================================================================================================
	// IGridProjection
	//=================================================================================================================

	JsonNode* JsonFormModel::grid_node ( int row, int column ) const
	{
		Q_UNUSED ( column );

		return value_node ( row );
	}

	JsonPointer JsonFormModel::grid_pointer ( int row, int column ) const
	{
		Q_UNUSED ( column );

		return pointer_for_row ( row );
	}

	GridPosition JsonFormModel::grid_cell ( const JsonPointer& pointer ) const
	{
		const int row = row_for_pointer ( pointer );

		return ( row >= 0 ) ? GridPosition { row, VALUE_COLUMN } : GridPosition {};
	}

	GridPosition JsonFormModel::grid_edit_cell ( int row, int column ) const
	{
		// A gesture edits the cell it was made on. This used to redirect a gesture on the key to the value it labels,
		// which was right while a key was only a label; now that a key can be renamed in place (EDIT-02) the redirect
		// would make renaming unreachable by the very gestures that perform it.

		return GridPosition { row, column };
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void JsonFormModel::handle_document_reset ()
	{
		clear_presentation ();
	}

	void JsonFormModel::handle_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		if ( objectNode == nullptr )
		{
			return;
		}

		// Re-resolve unconditionally: a replacement at or above the projected node swaps it out from under us and names
		// the replaced node rather than this one, so address comparison is the only reliable detection.

		JsonNode* const resolved = document->resolve ( objectPointer );

		const bool stillPresentable = ( resolved != nullptr ) &&
		                              ( ( resolved->kind () == JsonKind::Object ) ||
		                                ( resolved->is_scalar () && objectPointer.is_root () ) );

		if ( !stillPresentable )
		{
			clear_presentation ();

			return;
		}

		if ( resolved != objectNode )
		{
			objectNode = resolved;

			rebuild ();

			return;
		}

		if ( !covers ( pointer ) )
		{
			return;
		}

		// The common case: one field's value changed. Patch that row and nothing else, so the form refreshes in place
		// without re-measuring the label column (EDITOR-02's parity with the table's in-place cell refresh).

		if ( change == DocumentChange::ValueChanged )
		{
			const int row = row_for_descendant ( pointer );

			if ( row >= 0 )
			{
				emit dataChanged ( index ( row, KEY_COLUMN ), index ( row, VALUE_COLUMN ) );

				return;
			}
		}

		resync ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void JsonFormModel::rebuild ()
	{
		beginResetModel ();

		capture_rows ();

		endResetModel ();
	}

	void JsonFormModel::resync ()
	{
		// Rows are diffed by node ADDRESS; the shadow's pointers are never dereferenced, since after a removal they
		// name destroyed nodes.

		const bool wasScalarRoot = is_scalar_root_form ();

		if ( wasScalarRoot != objectNode->is_scalar () )
		{
			// The root changed between scalar and object. Every row means something different; reset.

			rebuild ();

			return;
		}

		if ( wasScalarRoot )
		{
			emit dataChanged ( index ( 0, KEY_COLUMN ), index ( 0, VALUE_COLUMN ) );

			return;
		}

		const int memberCount = objectNode->member_count ();

		// -- Pass 1: remove rows whose member value is gone, backwards in contiguous runs.

		QSet<const JsonNode*> liveMembers;

		for ( int memberIndex = 0; memberIndex < memberCount; ++memberIndex )
		{
			liveMembers.insert ( objectNode->member_value ( memberIndex ) );
		}

		auto is_removed = [ this, &liveMembers ] ( int slot )
		{
			return !liveMembers.contains ( rowNodes [ static_cast<size_t> ( slot ) ] );
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
			rowKeys .erase ( rowKeys .begin () + runStart, rowKeys .begin () + runEnd + 1 );

			endRemoveRows ();
		}

		// -- Pass 2: insert rows for members the shadow does not hold.

		QSet<const JsonNode*> shadowMembers;

		for ( JsonNode* const rowNode : rowNodes )
		{
			shadowMembers.insert ( rowNode );
		}

		int shadowCursor = 0;
		int memberIndex  = 0;

		while ( memberIndex < memberCount )
		{
			if ( shadowMembers.contains ( objectNode->member_value ( memberIndex ) ) )
			{
				++memberIndex;
				++shadowCursor;

				continue;
			}

			const int runStart = memberIndex;

			while ( ( memberIndex < memberCount ) &&
			        !shadowMembers.contains ( objectNode->member_value ( memberIndex ) ) )
			{
				++memberIndex;
			}

			const int runLength = memberIndex - runStart;

			beginInsertRows ( QModelIndex (), shadowCursor, shadowCursor + runLength - 1 );

			for ( int offset = 0; offset < runLength; ++offset )
			{
				rowNodes.insert ( rowNodes.begin () + shadowCursor + offset,
				                  objectNode->member_value ( runStart + offset ) );

				rowKeys.insert ( shadowCursor + offset, objectNode->member_key ( runStart + offset ) );
			}

			endInsertRows ();

			shadowCursor += runLength;
		}

		// -- Pass 3: reorder, as one move per out-of-place row so the view carries selection with it.

		for ( int target = 0; target < memberCount; ++target )
		{
			JsonNode* const wanted = objectNode->member_value ( target );

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

			std::rotate ( rowNodes.begin () + target, rowNodes.begin () + source, rowNodes.begin () + source + 1 );

			rowKeys.move ( source, target );

			endMoveRows ();
		}

		// -- Pass 4: relabel. A KeyRenamed arrives here, and it changes no row's identity -- only its label -- so it is
		//            invisible to the three passes above.

		for ( int row = 0; ( row < memberCount ) && ( row < rowKeys.size () ); ++row )
		{
			rowKeys [ row ] = objectNode->member_key ( row );
		}

		const int lastRow = rowCount () - 1;

		if ( lastRow >= 0 )
		{
			emit dataChanged ( index ( 0, KEY_COLUMN ), index ( lastRow, VALUE_COLUMN ) );
		}
	}

	void JsonFormModel::capture_rows ()
	{
		rowNodes.clear ();
		rowKeys .clear ();

		if ( objectNode->is_scalar () )
		{
			// The lone scalar root: one row, no key (EDITOR-02).

			rowNodes.push_back ( objectNode );
			rowKeys .append    ( QString () );

			return;
		}

		const int memberCount = objectNode->member_count ();

		rowNodes.reserve ( static_cast<size_t> ( memberCount ) );

		for ( int memberIndex = 0; memberIndex < memberCount; ++memberIndex )
		{
			rowNodes.push_back ( objectNode->member_value ( memberIndex ) );
			rowKeys .append    ( objectNode->member_key   ( memberIndex ) );
		}
	}

	bool JsonFormModel::is_scalar_root_form () const
	{
		return ( objectNode != nullptr ) && objectNode->is_scalar ();
	}

	bool JsonFormModel::covers ( const JsonPointer& pointer ) const
	{
		const int prefixLength = objectPointer.token_count ();

		if ( pointer.token_count () < prefixLength )
		{
			return false;
		}

		for ( int tokenIndex = 0; tokenIndex < prefixLength; ++tokenIndex )
		{
			if ( pointer.token ( tokenIndex ) != objectPointer.token ( tokenIndex ) )
			{
				return false;
			}
		}

		return true;
	}

	int JsonFormModel::row_for_descendant ( const JsonPointer& pointer ) const
	{
		// Walk up until the pointer names one of this form's rows, so a value change deep inside a "{...}" field still
		// identifies the field it belongs to.

		JsonPointer candidate = pointer;

		while ( candidate.token_count () >= objectPointer.token_count () )
		{
			const int row = row_for_pointer ( candidate );

			if ( row >= 0 )
			{
				return row;
			}

			if ( candidate.is_root () )
			{
				break;
			}

			candidate = candidate.parent ();
		}

		return -1;
	}

	bool JsonFormModel::is_renameable_row ( int row ) const
	{
		// Deliberately NOT is_editable_row. That asks whether the VALUE can be typed into, which has nothing to do with
		// whether the key naming it can be changed: an object's or an array's key renames perfectly well, and a null's
		// does too, even though none of their values open an editor.

		// SET-05: the user may switch key editing off entirely. Asked FIRST because it is the broadest refusal -- it is
		// not about this row at all -- and asked HERE rather than in flags() alone, because is_renameable_row is the
		// rule both flags() and the rename commit path consult (the same reason is_editable_row is shared).

		if ( !keyEditingAllowed )
		{
			return false;
		}

		if ( undo == nullptr )
		{
			return false;
		}

		// A scalar document root is one row with no key -- there is no member to rename, and nothing above it to
		// rename it within.

		if ( is_scalar_root_form () || ( objectNode == nullptr ) )
		{
			return false;
		}

		// A DUPLICATED key is renameable too. This carried the value path's duplicate-key guard, for its reason -- a
		// pointer named only the FIRST member with a key, so a rename aimed at the second would move the first. 15h.5
		// removed that reason (pointer_for_row records which occurrence a row is) and lifted the value path's guard;
		// this copy was left behind, and the MainWindow harness found it alongside Rename Key's.

		Q_UNUSED ( row );

		return true;
	}

	QStringList JsonFormModel::rival_keys_for_row ( int row ) const
	{
		// Every OTHER key in the object. The delegate turns this into the rule that a rename cannot be committed onto
		// one of them (VAL-02) -- stated as a set rather than as a yes/no so the check can run per keystroke without
		// the delegate having to ask the model each time.

		// SET-03a: with duplicates allowed there are no rivals, so JsonKeyValidator accepts every key and the editor
		// commits. THIS LIST IS WHERE THE SETTING HAD TO REACH -- the validator refuses a rival as Intermediate, which
		// QStyledItemDelegate honours by refusing to close the editor, so a duplicate key could not be typed however
		// permissive UndoController had become (reported 2026-08-28).

		if ( duplicateKeysAllowed )
		{
			return QStringList ();
		}

		QStringList rivals;

		for ( int index = 0; index < rowKeys.size (); ++index )
		{
			if ( index != row )
			{
				rivals.append ( rowKeys.at ( index ) );
			}
		}

		return rivals;
	}

	bool JsonFormModel::rename_row ( int row, const QString& newKey )
	{
		if ( !is_renameable_row ( row ) )
		{
			return false;
		}

		// UndoController enforces the duplicate rejection itself (EDIT-02), so a rename that slipped past the editor's
		// validator still cannot land -- flags() describes, setData() decides.

		return undo->rename_key ( pointer_for_row ( row ), newKey ) != EditOutcome::Rejected;
	}

	//=================================================================================================================
	// The provisional row (EDITOR-15)
	//=================================================================================================================

	int JsonFormModel::member_count () const
	{
		return static_cast<int> ( rowNodes.size () );
	}

	bool JsonFormModel::has_provisional_row () const
	{
		return provisionalActive;
	}

	bool JsonFormModel::is_provisional_row ( int row ) const
	{
		return provisionalActive && ( row == member_count () );
	}

	void JsonFormModel::set_provisional_row ( bool active )
	{
		// Only where a member could actually be added: a presented OBJECT. The lone scalar root has no members to grow
		// (EDITOR-02), and an unpresented model has nothing at all.

		// SET-05a is checked HERE as well as in present(), and the test that found the omission is the reason it is
		// stated twice rather than once at the call site: the row's first step is committing a key, so a row offered
		// with key editing off is a control nothing can be typed into. Both routes in must answer the same question.

		const bool wanted = active && is_presenting () && !is_scalar_root_form () && is_key_editing_allowed ()
		                 && ( undo != nullptr );

		if ( wanted == provisionalActive )
		{
			return;
		}

		const int row = member_count ();

		if ( wanted )
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

	bool JsonFormModel::materialize_provisional ( const QString& key )
	{
		// An EMPTY key abandons rather than creating a member named "" -- which is a legal JSON key, but not one a
		// user types by pressing Enter on an untouched field. The caller drops the row.

		if ( !provisionalActive || ( undo == nullptr ) || key.isEmpty () || !is_key_editing_allowed () )
		{
			return false;
		}

		// The member is created with a NULL value in one undoable step; EDITOR-13 then makes that null field an
		// ordinary typed-entry target, which is what lets the second half of "add a member" need no machinery of its
		// own. add_child appends, and enforces the VAL-02 duplicate refusal itself.

		if ( undo->add_child ( presented_pointer (), JsonKind::Null, key ) != EditOutcome::Applied )
		{
			return false;
		}

		// The document change has already rebuilt the rows through handle_node_changed, so the provisional row is
		// simply dropped -- the real member is in place beneath it.

		set_provisional_row ( false );

		return true;
	}

	QStringList JsonFormModel::rowKeys_as_rivals () const
	{
		// SET-03a, for rival_keys_for_row's reason: the provisional row's editor is gated by the same validator.

		if ( duplicateKeysAllowed )
		{
			return QStringList ();
		}

		// Every existing key, so the provisional row's editor refuses a duplicate as it is typed rather than at the
		// commit -- the same live guard the rename editor gets (VAL-02).

		return rowKeys;
	}

	bool JsonFormModel::remove_row ( int row )
	{
		// EDITOR-14: cut removes the member. The root guard and the undo step are UndoController's, which is why this
		// is three lines -- what the model owns is the row-to-pointer translation, and nothing else.

		if ( ( undo == nullptr ) || ( value_node ( row ) == nullptr ) )
		{
			return false;
		}

		return undo->delete_node ( pointer_for_row ( row ) ) == EditOutcome::Applied;
	}

	bool JsonFormModel::replace_row_value ( int row, std::unique_ptr<JsonNode> value )
	{
		// EDITOR-14: a field paste replaces the value wholesale, whatever either side's kind -- the conversion matrix
		// has already decided the paste is legal and produced the value to write (views/cell_paste_plan).

		if ( ( undo == nullptr ) || ( value == nullptr ) || ( value_node ( row ) == nullptr ) )
		{
			return false;
		}

		return undo->replace_subtree ( pointer_for_row ( row ), std::move ( value ), QStringLiteral ( "Paste" ) )
		     != EditOutcome::Rejected;
	}

	bool JsonFormModel::commit_row ( int row, const QVariant& value )
	{
		if ( !is_editable_row ( row ) )
		{
			return false;
		}

		JsonNode* const target = value_node ( row );

		const JsonPointer targetPointer = pointer_for_row ( row );

		EditOutcome outcome = EditOutcome::Rejected;

		// EDITOR-13: a null field takes the typed entry whole -- the committed text is read as a JSON literal, so the
		// field's new KIND comes from what was typed rather than from what it was. This is EDITOR-12's rule and it is
		// the same call the array table makes, so the two faces cannot interpret one keystroke differently.

		if ( target->kind () == JsonKind::Null )
		{
			std::unique_ptr<JsonNode> typed = typed_entry_value ( value.toString (), stringDisplay );

			// Null means the notation refused it -- a malformed escape. Refusing the COMMIT holds the caret on the
			// offending text, exactly as a malformed number does (VAL-03).

			if ( typed == nullptr )
			{
				return false;
			}

			return undo->replace_subtree ( targetPointer, std::move ( typed ), QStringLiteral ( "Edit Field" ) )
			     != EditOutcome::Rejected;
		}

		switch ( target->kind () )
		{
			case JsonKind::String:
			{
				// The editor's text is in the SET-03 notation, so it is read back through the same rule that produced
				// it -- string_commit_value is the exact inverse of string_edit_text under one mode. A malformed escape
				// refuses here rather than committing something the user did not type; the delegate's validator
				// normally stops it reaching this far, but a paste or a programmatic set can.

				QString committed;

				if ( !string_commit_value ( value.toString (), stringDisplay, committed ) )
				{
					return false;
				}

				outcome = undo->set_string ( targetPointer, committed );

				break;
			}

			case JsonKind::Number:
			{
				// VAL-03, refused here so the view keeps the editor open on the errored field rather than reverting it.

				const QString token = value.toString ().trimmed ();

				if ( !Validator::is_valid_number ( token ) )
				{
					return false;
				}

				outcome = undo->set_number ( targetPointer, token );

				break;
			}

			case JsonKind::Boolean:
			{
				const bool booleanValue = ( value.userType () == QMetaType::Bool )
				                        ? value.toBool ()
				                        : ( value.toString ().compare ( cell_text::BOOLEAN_TRUE, Qt::CaseInsensitive ) == 0 );

				outcome = undo->set_boolean ( targetPointer, booleanValue );

				break;
			}

			default:
			{
				return false;
			}
		}

		return outcome != EditOutcome::Rejected;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		return static_cast<int> ( rowNodes.size () );
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
				return cell_display_text ( node );
			}

			case Qt::EditRole:
			{
				return cell_edit_text ( node );
			}

			case Qt::ToolTipRole:
			{
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

	Qt::ItemFlags JsonFormModel::flags ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return Qt::NoItemFlags;
		}

		// Both columns are landable: Up / Down move the highlight over every field, and the key column is where the
		// EDITOR-02 right-click acts. Only the value column of an editable scalar opens an editor.

		Qt::ItemFlags fieldFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

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

		if ( !is_editable_cell ( value_node ( row ) ) )
		{
			return false;
		}

		// The duplicate-key guard (see the header). A pointer names the FIRST member with a key, so committing against
		// the second would write the first -- silently, and with a correct-looking undo entry.

		const bool keyIsDuplicated = !is_scalar_root_form () && ( objectNode->key_count ( key_for_row ( row ) ) > 1 );

		return !keyIsDuplicated;
	}

	bool JsonFormModel::setData ( const QModelIndex& index, const QVariant& value, int role )
	{
		const bool isCommit = index.isValid () && ( role == Qt::EditRole ) && ( undo != nullptr );

		if ( !isCommit )
		{
			return false;
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

		return rowInRange ? objectPointer.child ( rowKeys.at ( row ) ) : objectPointer;
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

		// First match, mirroring JsonPointer::resolve -- the same rule that makes duplicate keys read-only above.

		return rowKeys.indexOf ( pointer.token ( objectPointer.token_count () ) );
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

		// The same duplicate-key guard the value path carries, for the same reason: a JSON Pointer names the FIRST
		// member with a key, so a rename aimed at the second would move the first.

		return objectNode->key_count ( key_for_row ( row ) ) <= 1;
	}

	QStringList JsonFormModel::rival_keys_for_row ( int row ) const
	{
		// Every OTHER key in the object. The delegate turns this into the rule that a rename cannot be committed onto
		// one of them (VAL-02) -- stated as a set rather than as a yes/no so the check can run per keystroke without
		// the delegate having to ask the model each time.

		QStringList rivals;

		const QString ownKey = key_for_row ( row );

		for ( int index = 0; index < rowKeys.size (); ++index )
		{
			if ( index != row )
			{
				rivals.append ( rowKeys.at ( index ) );
			}
		}

		// A row whose own key is duplicated is not renameable at all, so its key never reaches this list as a rival of
		// itself -- but the guard above is what enforces that, not this.

		Q_UNUSED ( ownKey );

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

	bool JsonFormModel::commit_row ( int row, const QVariant& value )
	{
		if ( !is_editable_row ( row ) )
		{
			return false;
		}

		JsonNode* const target = value_node ( row );

		const JsonPointer targetPointer = pointer_for_row ( row );

		EditOutcome outcome = EditOutcome::Rejected;

		switch ( target->kind () )
		{
			case JsonKind::String:
			{
				outcome = undo->set_string ( targetPointer, value.toString () );

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

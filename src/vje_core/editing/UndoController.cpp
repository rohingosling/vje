//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   UndoController implementation. See UndoController.hpp for the operation contract and the list of preconditions
//   enforced here.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/UndoController.hpp>
#include <vje_core/editing/EditCommands.hpp>
#include <vje_core/editing/edit_transforms.hpp>
#include <vje_core/editing/json_order.hpp>
#include <vje_core/editing/paste_target_plan.hpp>

#include <QPair>

#include <algorithm>

namespace vje
{
	namespace
	{
		// Closes a change batch however the scope is left. A begin without an end leaves the document silent for the
		// rest of the session, which is a far worse failure than the cost it exists to avoid.

		struct BatchGuard
		{
			JsonDocument* document;

			~BatchGuard () { document->end_change_batch (); }
		};
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	UndoController::UndoController ( JsonDocument* document, QObject* parent )
		: QObject   ( parent )
		, document  ( document )
		, undoStack ( this )
	{
		// UNDO-04: the dirty flag is the inverse of the stack's clean state. Undoing back to the last-saved point
		// clears the modified indicator; any edit away from it sets it.

		connect
		(
			&undoStack, &QUndoStack::cleanChanged,
			this,       [ this ] ( bool clean ) { this->document->set_dirty ( !clean ); }
		);
	}

	//=================================================================================================================
	// VAL-02 / SET-03a -- the duplicate-key policy
	//=================================================================================================================

	void UndoController::set_allow_duplicate_keys ( bool allow )
	{
		// Nothing is re-validated when this changes, and nothing needs to be: it governs what the NEXT edit is allowed
		// to do. Switching it off does not go looking for duplicates a document already carries -- those are legal and
		// preserved (FILE-04), and removing them is Rename Key's job, on the user's own say-so.

		allowDuplicateKeys = allow;
	}

	bool UndoController::allow_duplicate_keys () const
	{
		return allowDuplicateKeys;
	}

	//=================================================================================================================
	// Stack Interface
	//=================================================================================================================

	QUndoStack* UndoController::stack () const
	{
		return const_cast<QUndoStack*> ( &undoStack );
	}

	bool UndoController::can_undo () const
	{
		return undoStack.canUndo ();
	}

	bool UndoController::can_redo () const
	{
		return undoStack.canRedo ();
	}

	void UndoController::undo ()
	{
		replay ( false );
	}

	void UndoController::redo ()
	{
		replay ( true );
	}

	bool UndoController::is_replaying () const
	{
		return replaying;
	}

	void UndoController::replay ( bool forward )
	{
		// A macro's children are re-applied ONE AT A TIME on the way back, each notifying, so undoing a grouped
		// command costs exactly what applying it did. Batched for the same reason and with the same guarantee.
		//
		// UNDO-05: the flag spans the batch's CLOSE as well as the stack call, and that is the whole of its placement.
		// The batch is what notifies -- the rows a step brings back reach a view as the guard below is destroyed -- so
		// a flag cleared right after undoStack.undo () would be false by the time anyone could ask it.

		replaying = true;

		{
			document->begin_change_batch ();

			const BatchGuard guard { document };

			if ( forward )
			{
				undoStack.redo ();
			}
			else
			{
				undoStack.undo ();
			}
		}

		replaying = false;

		emit replayed ();
	}

	bool UndoController::is_clean () const
	{
		return undoStack.isClean ();
	}

	void UndoController::set_clean ()
	{
		undoStack.setClean ();
	}

	void UndoController::clear ()
	{
		undoStack.clear ();
	}

	//=================================================================================================================
	// Grouping (EDIT-14 / EDIT-10)
	//=================================================================================================================

	UndoController::MacroScope::MacroScope ( UndoController& controller, const QString& text )
		: controller ( controller )
	{
		// Nothing is opened here -- see the header. The text is remembered so push_command can open the macro with it
		// at the moment a group turns out to have something in it.

		// A scope inside a live one is a no-op: the outer scope owns the group, and touching this state here would
		// close its macro the moment this object went out of scope.

		owned = !( controller.macroPending || controller.macroOpen );

		if ( !owned )
		{
			return;
		}

		controller.macroText    = text;
		controller.macroPending = true;
		controller.macroOpen    = false;

		// ONE GESTURE, ONE NOTIFICATION (NFR-03). Every command pushed inside this scope would otherwise notify on its
		// own, and every observer of the document would re-derive its projection once per command -- which is the
		// quadratic that made a 400-element column delete take seconds with the Code tab open.

		controller.document->begin_change_batch ();
	}

	UndoController::MacroScope::~MacroScope ()
	{
		if ( !owned )
		{
			return;
		}

		if ( controller.macroOpen )
		{
			controller.undoStack.endMacro ();
		}

		// Closing the batch is what actually emits: one node_changed naming the subtree the group touched.

		controller.document->end_change_batch ();

		controller.macroPending = false;
		controller.macroOpen    = false;
		controller.macroText.clear ();
	}

	bool UndoController::MacroScope::pushed () const
	{
		return controller.macroOpen;
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void UndoController::push_command ( QUndoCommand* command )
	{
		// The lazy half of MacroScope: the FIRST command inside a live scope opens the macro, and one that never
		// arrives leaves the stack untouched.

		if ( macroPending )
		{
			undoStack.beginMacro ( macroText );

			macroPending = false;
			macroOpen    = true;
		}

		undoStack.push ( command );
	}

	std::unique_ptr<JsonNode> UndoController::make_default ( JsonKind kind )
	{
		switch ( kind )
		{
			case JsonKind::Null:    return JsonNode::make_null ();
			case JsonKind::Boolean: return JsonNode::make_boolean ( false );
			case JsonKind::Number:  return JsonNode::make_number ( QStringLiteral ( "0" ) );
			case JsonKind::String:  return JsonNode::make_string ( QString () );
			case JsonKind::Array:   return JsonNode::make_array ();
			case JsonKind::Object:  return JsonNode::make_object ();
		}

		return JsonNode::make_null ();
	}

	EditOutcome UndoController::insert_at
	(
		const JsonPointer&        parentPointer,
		JsonNode*                 parentNode,
		int                       index,
		const QString&            key,
		std::unique_ptr<JsonNode> node,
		const QString&            text
	)
	{
		const bool parentIsObject = ( parentNode->kind () == JsonKind::Object );

		// VAL-02: an object cannot gain a duplicate sibling key through an edit -- unless SET-03a says the user wants
		// the duplicates RFC 8259 permits. Asked through Validator rather than spelled inline, so the rule this and
		// rename_key apply is one function rather than two `has_member` calls that agree until one of them is edited.

		if ( !allowDuplicateKeys && Validator::introduces_duplicate ( *parentNode, key ) )
		{
			return EditOutcome::Rejected;
		}

		InsertNodeCommand* command = new InsertNodeCommand ( document, parentPointer, index, parentIsObject, key, std::move ( node ) );
		command->setText ( text );

		push_command ( command );

		return EditOutcome::Applied;
	}

	//=================================================================================================================
	// Scalar Value Edits (EDIT-01)
	//=================================================================================================================

	EditOutcome UndoController::set_string ( const JsonPointer& target, const QString& text )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::String ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->string_value () == text )
		{
			return EditOutcome::Unchanged;
		}

		push_command ( new SetValueCommand ( document, target, JsonNode::make_string ( text ) ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::set_number ( const JsonPointer& target, const QString& token )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Number ) )
		{
			return EditOutcome::Rejected;
		}

		// VAL-03: a number edit must be a valid JSON number, or it is rejected without altering the document.

		if ( !edit_transforms::is_json_number ( token ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->number_token () == token )
		{
			return EditOutcome::Unchanged;
		}

		push_command ( new SetValueCommand ( document, target, JsonNode::make_number ( token ) ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::set_boolean ( const JsonPointer& target, bool value )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Boolean ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->boolean_value () == value )
		{
			return EditOutcome::Unchanged;
		}

		push_command ( new SetValueCommand ( document, target, JsonNode::make_boolean ( value ) ) );

		return EditOutcome::Applied;
	}

	//=================================================================================================================
	// Structural Edits
	//=================================================================================================================

	EditOutcome UndoController::rename_key ( const JsonPointer& target, const QString& newKey )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( ( parent == nullptr ) || ( parent->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		const int     index  = node->index_in_parent ();
		const QString oldKey = parent->member_key ( index );

		if ( oldKey == newKey )
		{
			return EditOutcome::Unchanged;
		}

		// VAL-02: newKey differs from oldKey, so any existing occurrence is a genuine collision -- which SET-03a may
		// nevertheless permit. No ignoreIndex is needed: the equality above has already excluded the member's own slot.

		if ( !allowDuplicateKeys && Validator::introduces_duplicate ( *parent, newKey ) )
		{
			return EditOutcome::Rejected;
		}

		push_command ( new RenameKeyCommand ( document, target.parent (), index, newKey ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::add_node ( const JsonPointer& selection, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( selection );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		// EDIT-03 placement: a container takes the new node as its last child; a scalar takes it as its next sibling.

		return node->is_container () ? add_child ( selection, kind, key ) : add_sibling ( selection, kind, key );
	}

	EditOutcome UndoController::add_child ( const JsonPointer& container, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( container );

		if ( ( node == nullptr ) || !node->is_container () )
		{
			return EditOutcome::Rejected;
		}

		const int index = ( node->kind () == JsonKind::Object ) ? node->member_count () : node->array_size ();

		return insert_at ( container, node, index, key, make_default ( kind ), QStringLiteral ( "Add Node" ) );
	}

	EditOutcome UndoController::add_sibling ( const JsonPointer& target, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root has no sibling.
		}

		const int index = node->index_in_parent () + 1;

		return insert_at ( target.parent (), parent, index, key, make_default ( kind ), QStringLiteral ( "Add Node" ) );
	}

	EditOutcome UndoController::delete_node ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root is not deletable.
		}

		const bool parentIsObject = ( parent->kind () == JsonKind::Object );
		const int  index          = node->index_in_parent ();

		push_command ( new RemoveNodeCommand ( document, target.parent (), index, parentIsObject ) );

		return EditOutcome::Applied;
	}

	int UndoController::delete_nodes ( const QList<JsonPointer>& targets )
	{
		// Resolved to positions FIRST, while every pointer still names what it named when the selection was made. The
		// sort is what the header is about; doing it here rather than trusting the caller to arrive sorted means the
		// rule holds for whichever surface calls this next.

		QList<QPair<int, JsonPointer>> ordered;

		ordered.reserve ( targets.size () );

		for ( const JsonPointer& target : targets )
		{
			JsonNode* const node = document->resolve ( target );

			if ( ( node == nullptr ) || ( node->parent () == nullptr ) )
			{
				continue;
			}

			ordered.append ( { node->index_in_parent (), target } );
		}

		std::sort
		(
			ordered.begin (), ordered.end (),
			[] ( const QPair<int, JsonPointer>& left, const QPair<int, JsonPointer>& right )
			{
				return left.first > right.first;
			}
		);

		int removed = 0;

		{
			MacroScope macro ( *this, QStringLiteral ( "Delete Nodes" ) );

			for ( const QPair<int, JsonPointer>& entry : ordered )
			{
				if ( delete_node ( entry.second ) == EditOutcome::Applied )
				{
					++removed;
				}
			}
		}

		return removed;
	}

	EditOutcome UndoController::duplicate_node ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root cannot be duplicated in place.
		}

		const bool parentIsObject = ( parent->kind () == JsonKind::Object );
		const int  index          = node->index_in_parent ();

		QString key;

		if ( parentIsObject )
		{
			key = edit_transforms::duplicate_key_name ( *parent, parent->member_key ( index ) );
		}

		// Insert the clone immediately after the original (arrays) / with a de-duplicated key (objects).

		InsertNodeCommand* command =
			new InsertNodeCommand ( document, target.parent (), index + 1, parentIsObject, key, node->clone () );
		command->setText ( QStringLiteral ( "Duplicate Node" ) );

		push_command ( command );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::move_node ( const JsonPointer& target, MoveDirection direction )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root has no siblings to reorder among.
		}

		const int index = node->index_in_parent ();
		const int count = ( parent->kind () == JsonKind::Object ) ? parent->member_count () : parent->array_size ();
		const int to    = ( direction == MoveDirection::Up ) ? ( index - 1 ) : ( index + 1 );

		if ( ( to < 0 ) || ( to >= count ) )
		{
			return EditOutcome::Unchanged;                         // Already at the edge.
		}

		push_command ( new MoveNodeCommand ( document, target.parent (), index, to ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::move_child ( const JsonPointer& parentPointer, int fromIndex, int toIndex )
	{
		JsonNode* parent = document->resolve ( parentPointer );

		if ( ( parent == nullptr ) || !parent->is_container () )
		{
			return EditOutcome::Rejected;
		}

		const int count = ( parent->kind () == JsonKind::Object ) ? parent->member_count () : parent->array_size ();

		// Both indices name an EXISTING child, unlike the insert primitives, whose upper bound is one past the end:
		// a move lifts a child out and puts it back, so the container never changes size and there is no slot at
		// count to move into.

		if ( ( fromIndex < 0 ) || ( fromIndex >= count ) || ( toIndex < 0 ) || ( toIndex >= count ) )
		{
			return EditOutcome::Rejected;
		}

		if ( fromIndex == toIndex )
		{
			return EditOutcome::Unchanged;
		}

		push_command ( new MoveNodeCommand ( document, parentPointer, fromIndex, toIndex ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::sort_array
	(
		const JsonPointer&            arrayPointer,
		const std::optional<QString>& memberKey,
		Qt::SortOrder                 order
	)
	{
		JsonNode* const array = document->resolve ( arrayPointer );

		if ( ( array == nullptr ) || ( array->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		if ( array->array_size () < 2 )
		{
			return EditOutcome::Unchanged;
		}

		const std::vector<int> permutation = json_order::sort_permutation ( *array, memberKey, order );

		// The identity check is what makes an already-sorted array a reported no-op rather than a pushed command that
		// undoes nothing -- lesson D19's defect, which replace_subtree carried undetected for six phases. It is
		// cheap here because the permutation has to be computed either way.

		if ( json_order::is_identity ( permutation ) )
		{
			return EditOutcome::Unchanged;
		}

		push_command ( new SortArrayCommand ( document, arrayPointer, permutation ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::paste_value_list
	(
		const JsonPointer&            target,
		std::vector<PastedValue>&     values,
		const QString&                listName,
		PasteRoute                    route,
		const std::optional<QString>& targetColumnName,
		QString*                      refusal,
		QString*                      pastedName
	)
	{
		JsonNode* const node = document->resolve ( target );

		const PasteTargetPlan plan = plan_paste_target ( node, listName, route, targetColumnName );

		if ( plan.is_refused () )
		{
			if ( refusal != nullptr )
			{
				*refusal = plan.refusal;
			}

			return EditOutcome::Rejected;
		}

		if ( pastedName != nullptr )
		{
			*pastedName = plan.name;
		}

		// An INSERT goes in front of the column aimed at; everything else goes last. Where a single-column array is
		// reshaped, the column aimed at IS the reshaped values' own -- the header had no member key to name it by.

		std::optional<QString> insertBefore;

		if ( route == PasteRoute::InsertColumn )
		{
			insertBefore = ( plan.kind == PasteTargetKind::ConvertToArrayOfObjects )
			             ? std::optional<QString> ( plan.existingName )
			             : targetColumnName;
		}

		// One group for the whole gesture, opened lazily -- a paste whose values are all absent creates nothing and
		// must leave no undo step behind (lesson Q43). Nesting is safe, so a caller may already hold one.

		MacroScope macro ( *this, QStringLiteral ( "Paste" ) );

		switch ( plan.kind )
		{
			case PasteTargetKind::Object:
			{
				// One member holding an array of the values. An ABSENT value is written as `null`: the array has a
				// slot for every value the column carried, and a list has no absent slot.

				std::unique_ptr<JsonNode> list = JsonNode::make_array ();

				for ( PastedValue& entry : values )
				{
					list->append_element ( ( entry.value != nullptr ) ? std::move ( entry.value ) : JsonNode::make_null () );
				}

				insert_member_at ( target, node->member_count (), plan.name, std::move ( list ), QStringLiteral ( "Paste" ) );

				break;
			}

			case PasteTargetKind::EmptyArray:
			case PasteTargetKind::OneDimensionalArray:
			{
				// Overwrite element by element, then append the rest as BARE elements -- a one-dimensional array holds
				// values rather than named members. The empty array is the same rule with nothing to overwrite.

				for ( std::size_t index = 0; index < values.size (); ++index )
				{
					if ( values [ index ].value == nullptr )
					{
						// An absent value leaves an EXISTING cell untouched, and writes null where it must create one.

						if ( static_cast<int> ( index ) < node->array_size () )
						{
							continue;
						}

						append_element ( target, JsonNode::make_null (), QStringLiteral ( "Paste" ) );

						continue;
					}

					if ( static_cast<int> ( index ) < node->array_size () )
					{
						replace_subtree
						(
							target.child ( QString::number ( static_cast<int> ( index ) ) ),
							std::move ( values [ index ].value ),
							QStringLiteral ( "Paste" )
						);
					}
					else
					{
						append_element ( target, std::move ( values [ index ].value ), QStringLiteral ( "Paste" ) );
					}
				}

				break;
			}

			case PasteTargetKind::ConvertToArrayOfObjects:
			{
				convert_array_and_paste_column ( target, node, values, plan.name, plan.existingName, insertBefore );

				break;
			}

			case PasteTargetKind::ArrayOfObjects:
			{
				paste_column_into_objects ( target, node, values, plan.name, insertBefore );

				break;
			}

			case PasteTargetKind::Refused:
			{
				break;
			}
		}

		return macro.pushed () ? EditOutcome::Applied : EditOutcome::Unchanged;
	}

	void UndoController::convert_array_and_paste_column
	(
		const JsonPointer&            arrayPointer,
		JsonNode*                     array,
		std::vector<PastedValue>&     values,
		const QString&                name,
		const QString&                existingName,
		const std::optional<QString>& insertBefore
	)
	{
		// TWO STEPS, ONE GESTURE. Reshape first, then paste the column into the reshaped array -- which is what makes
		// this a composition of rules that already exist rather than a fourth paste implementation. paste_value_list
		// has already opened a MacroScope and this opens none of its own: the whole conversion is one undo step, and
		// Ctrl+Z returns the array to the bare list it was.
		//
		// The reshape goes through replace_subtree ELEMENT BY ELEMENT rather than replacing the array wholesale. Both
		// are one undo step inside the macro, but replacing the array would make the undo stack's memory proportional
		// to the whole array twice over, and an element-wise walk is what every other command here does.

		const int originalCount = array->array_size ();

		for ( int row = 0; row < originalCount; ++row )
		{
			const JsonNode* const element = array->array_element ( row );

			if ( element == nullptr )
			{
				continue;
			}

			// An element that is ALREADY an object keeps its own members and simply gains the new column below. Only
			// the bare values need wrapping -- which is what makes an array only PARTLY of objects (still
			// single-column under EDIT-16's definition) come out coherent rather than doubly wrapped.

			if ( element->kind () == JsonKind::Object )
			{
				continue;
			}

			std::unique_ptr<JsonNode> wrapper = JsonNode::make_object ();

			wrapper->append_member ( existingName, element->clone () );

			replace_subtree ( arrayPointer.child ( QString::number ( row ) ), std::move ( wrapper ), QStringLiteral ( "Paste" ) );
		}

		// And now it IS an array of objects, so the column paste is the ordinary one. The name is the plan's, already
		// de-duplicated against existingName where the two would collide.

		paste_column_into_objects ( arrayPointer, array, values, name, insertBefore );
	}

	void UndoController::paste_column_into_objects
	(
		const JsonPointer&            arrayPointer,
		JsonNode*                     array,
		std::vector<PastedValue>&     values,
		const QString&                name,
		const std::optional<QString>& insertBefore
	)
	{
		// WHERE the new member goes in each element. Last, unless the paste is an insert in front of a named column --
		// and then in front of that column where the element has it, and in front of the next column along where it
		// does not. That second half is why the order is taken from the whole array before anything changes: a ragged
		// element lacking the anchor still has to put the new member where the table will show it, which is next to
		// the columns that follow the anchor rather than at the element's end.

		QStringList columnOrder;

		if ( insertBefore.has_value () )
		{
			for ( int row = 0; row < array->array_size (); ++row )
			{
				const JsonNode* const element = array->array_element ( row );

				if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
				{
					continue;
				}

				for ( int member = 0; member < element->member_count (); ++member )
				{
					const QString key = element->member_key ( member );

					if ( !columnOrder.contains ( key ) )
					{
						columnOrder.append ( key );
					}
				}
			}
		}

		const auto insertion_index = [ &columnOrder, &insertBefore ] ( const JsonNode& element )
		{
			const int anchor = insertBefore.has_value () ? columnOrder.indexOf ( insertBefore.value () ) : -1;

			if ( anchor >= 0 )
			{
				for ( int column = anchor; column < columnOrder.size (); ++column )
				{
					if ( const JsonNode* const member = element.find_member ( columnOrder.at ( column ) ) )
					{
						return member->index_in_parent ();
					}
				}
			}

			return element.member_count ();
		};

		// Is this a column the array ALREADY carries? The answer decides the fill: a NEW column is filled with `null`
		// on every element the values do not reach, while an EXISTING one leaves those elements exactly as they were
		// -- EDITOR-18's shorter-source rule, which a fill applied to an existing column would quietly violate.

		bool columnExists = false;

		for ( int row = 0; row < array->array_size (); ++row )
		{
			const JsonNode* const element = array->array_element ( row );

			if ( ( element != nullptr ) && ( element->kind () == JsonKind::Object ) && element->has_member ( name ) )
			{
				columnExists = true;

				break;
			}
		}

		const int originalCount = array->array_size ();

		// The values the array already has room for.

		for ( int row = 0; row < originalCount; ++row )
		{
			const JsonPointer elementPointer = arrayPointer.child ( QString::number ( row ) );
			const JsonPointer memberPointer  = elementPointer.child ( name );

			JsonNode* const element = array->array_element ( row );

			if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
			{
				continue;
			}

			std::unique_ptr<JsonNode> value;

			if ( row < static_cast<int> ( values.size () ) )
			{
				value = std::move ( values [ static_cast<std::size_t> ( row ) ].value );
			}

			if ( value == nullptr )
			{
				// Past the values, or an absent one. A NEW column is filled with null here; an existing column's
				// unreached elements keep what they had.

				if ( !columnExists )
				{
					insert_member_at ( elementPointer, insertion_index ( *element ), name, JsonNode::make_null (), QStringLiteral ( "Paste" ) );
				}

				continue;
			}

			if ( element->has_member ( name ) )
			{
				replace_subtree ( memberPointer, std::move ( value ), QStringLiteral ( "Paste" ) );
			}
			else
			{
				insert_member_at ( elementPointer, insertion_index ( *element ), name, std::move ( value ), QStringLiteral ( "Paste" ) );
			}
		}

		// Values past the end APPEND an element carrying the value under that name -- in either branch, new column or
		// existing -- with `null` under every other column the array already has, so the paste leaves what it created
		// uniform (EDIT-16).

		QStringList existingColumns;

		for ( int row = 0; row < originalCount; ++row )
		{
			const JsonNode* const element = array->array_element ( row );

			if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
			{
				continue;
			}

			for ( int member = 0; member < element->member_count (); ++member )
			{
				const QString key = element->member_key ( member );

				if ( !existingColumns.contains ( key ) )
				{
					existingColumns.append ( key );
				}
			}
		}

		if ( !existingColumns.contains ( name ) )
		{
			existingColumns.append ( name );
		}

		for ( std::size_t index = static_cast<std::size_t> ( originalCount ); index < values.size (); ++index )
		{
			std::unique_ptr<JsonNode> element = JsonNode::make_object ();

			for ( const QString& key : existingColumns )
			{
				element->append_member ( key, JsonNode::make_null () );
			}

			std::unique_ptr<JsonNode>& value = values [ index ].value;

			if ( JsonNode* const slot = element->find_member ( name ) )
			{
				element->replace_member_value
				(
					slot->index_in_parent (),
					( value != nullptr ) ? std::move ( value ) : JsonNode::make_null ()
				);
			}

			append_element ( arrayPointer, std::move ( element ), QStringLiteral ( "Paste" ) );
		}
	}

	EditOutcome UndoController::change_type ( const JsonPointer& target, JsonKind newKind )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		if ( node->kind () == newKind )
		{
			return EditOutcome::Unchanged;                         // Converting to the current type is a no-op.
		}

		return replace_subtree ( target, edit_transforms::convert_node ( *node, newKind ), QStringLiteral ( "Change Type" ) );
	}

	EditOutcome UndoController::normalize_array ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		// EDIT-11 is enabled only for an array whose elements are all objects.

		for ( int index = 0; index < node->array_size (); ++index )
		{
			if ( node->array_element ( index )->kind () != JsonKind::Object )
			{
				return EditOutcome::Rejected;
			}
		}

		return replace_subtree
		(
			target,
			edit_transforms::normalize_array_elements ( *node ),
			QStringLiteral ( "Normalize Array Elements" )
		);
	}

	EditOutcome UndoController::array_to_objects ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		return replace_subtree ( target, edit_transforms::array_to_object ( *node ), QStringLiteral ( "Convert Array to Objects" ) );
	}

	EditOutcome UndoController::objects_to_array ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		return replace_subtree ( target, edit_transforms::object_to_array ( *node ), QStringLiteral ( "Convert Objects to Array" ) );
	}

	EditOutcome UndoController::replace_subtree ( const JsonPointer& target, std::unique_ptr<JsonNode> newSubtree, const QString& text )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		// A REPLACEMENT THAT CHANGES NOTHING IS Unchanged, NOT Applied (2026-08-02, Phase 15 / VAL-04). Pushing it would
		// dirty the document and add an undo step for an edit the user cannot see, and -- worse -- would make every
		// caller report a success that did not happen.
		//
		// It is reachable from three directions, so the check belongs here rather than in any one of them:
		//
		//   normalize_array   -- normalize_array_elements is IDEMPOTENT (edit_transforms.hpp), so normalizing an
		//                        already-uniform array rebuilds the identical subtree. This is the common case, and
		//                        the one that has to be distinguishable from a broken command.
		//   replace_subtree   -- a Code View commit with no textual change re-parses to an equal document. CodeView
		//                        already branches on Unchanged for exactly this ("No changes to apply."), which until
		//                        now was unreachable.
		//   JsonTableModel    -- a cell replaced with the value it already held.
		//
		// The comparison is O(n) in the subtree, against callers that have ALREADY built that subtree in O(n) -- so it
		// is a constant factor on work already being done, not a new order of cost (NFR-03).

		if ( ( newSubtree != nullptr ) && node->equals ( *newSubtree ) )
		{
			return EditOutcome::Unchanged;
		}

		const bool targetIsRoot    = ( node->parent () == nullptr );
		int        childIndex      = -1;
		bool       parentIsObject  = false;

		if ( !targetIsRoot )
		{
			JsonNode* parent = node->parent ();
			childIndex       = node->index_in_parent ();
			parentIsObject   = ( parent->kind () == JsonKind::Object );
		}

		// push_command, NOT undoStack.push. This was the one push site that bypassed it, which is exactly the escape
		// its own comment warns about: a replacement made inside a live MacroScope pushed straight onto the stack and
		// left the group, so EDITOR-18's column paste -- the first caller to group several replacements -- produced
		// one undo step per cell instead of one per gesture. Latent since Phase 9, and invisible until something
		// grouped this call.

		push_command
		(
			new ReplaceNodeCommand
			(
				document, target, childIndex, parentIsObject, targetIsRoot,
				std::move ( newSubtree ), DocumentChange::SubtreeReplaced, text
			)
		);

		return EditOutcome::Applied;
	}

	//=================================================================================================================
	// Placement inserts (Phase 9)
	//=================================================================================================================

	EditOutcome UndoController::insert_element_at ( const JsonPointer& arrayPointer, int index, std::unique_ptr<JsonNode> element, const QString& text )
	{
		JsonNode* node = document->resolve ( arrayPointer );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		if ( ( index < 0 ) || ( index > node->array_size () ) )
		{
			return EditOutcome::Rejected;
		}

		return insert_at ( arrayPointer, node, index, QString (), std::move ( element ), text );
	}

	EditOutcome UndoController::append_element ( const JsonPointer& arrayPointer, std::unique_ptr<JsonNode> element, const QString& text )
	{
		JsonNode* node = document->resolve ( arrayPointer );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		return insert_element_at ( arrayPointer, node->array_size (), std::move ( element ), text );
	}

	EditOutcome UndoController::insert_member_at ( const JsonPointer& objectPointer, int index, const QString& key, std::unique_ptr<JsonNode> value, const QString& text )
	{
		JsonNode* node = document->resolve ( objectPointer );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		if ( ( index < 0 ) || ( index > node->member_count () ) )
		{
			return EditOutcome::Rejected;
		}

		// insert_at applies the VAL-02 duplicate-key check for an object parent.

		return insert_at ( objectPointer, node, index, key, std::move ( value ), text );
	}

	EditOutcome UndoController::paste_node ( const JsonPointer& selection, std::unique_ptr<JsonNode> node, const QString& objectKey )
	{
		JsonNode* target = document->resolve ( selection );

		if ( ( target == nullptr ) || ( node == nullptr ) )
		{
			return EditOutcome::Rejected;
		}

		// EDIT-03's placement rule, re-used verbatim: a container receives the paste as its last child; a scalar as
		// the sibling immediately after itself.

		JsonNode*   receiver    = nullptr;
		JsonPointer receiverPointer;
		int         insertIndex = 0;

		if ( target->is_container () )
		{
			receiver        = target;
			receiverPointer = selection;
			insertIndex     = ( target->kind () == JsonKind::Object ) ? target->member_count () : target->array_size ();
		}
		else
		{
			receiver = target->parent ();

			if ( receiver == nullptr )
			{
				return EditOutcome::Rejected;               // A lone scalar root has no container and no sibling slot.
			}

			receiverPointer = selection.parent ();
			insertIndex     = target->index_in_parent () + 1;
		}

		if ( receiver->kind () == JsonKind::Object )
		{
			// Synthesize when no source key is carried; de-duplicate a collision so the paste is never refused for a
			// clash the user did not choose.

			QString key = objectKey.isEmpty () ? QStringLiteral ( "item" ) : objectKey;

			if ( receiver->has_member ( key ) )
			{
				key = edit_transforms::duplicate_key_name ( *receiver, key );
			}

			return insert_member_at ( receiverPointer, insertIndex, key, std::move ( node ), QStringLiteral ( "Paste Node" ) );
		}

		return insert_element_at ( receiverPointer, insertIndex, std::move ( node ), QStringLiteral ( "Paste Node" ) );
	}
}

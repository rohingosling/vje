//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   paste_target_plan implementation. See the header for why EDIT-16's decision is one function with two callers.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/paste_target_plan.hpp>

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/edit_transforms.hpp>
#include <vje_core/services/column_naming.hpp>

#include <QCoreApplication>

namespace vje
{
	bool renders_single_column ( const JsonNode& array )
	{
		if ( array.kind () != JsonKind::Array )
		{
			return false;
		}

		const int elementCount = array.array_size ();

		// An EMPTY array is single-column: there is no key union to derive from nothing. JsonTableModel's projection
		// says the same, and EDIT-16's empty-array case is the one that acts on it.

		if ( elementCount == 0 )
		{
			return true;
		}

		for ( int index = 0; index < elementCount; ++index )
		{
			const JsonNode* const element = array.array_element ( index );

			if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
			{
				return true;
			}
		}

		return false;
	}

	namespace
	{
		// EDIT-16 / VAL-02. The list's own name where it is free, EDIT-07's sequence where it is taken. `taken` is
		// asked of whatever namespace the name is landing in -- an object's members, or the columns of an array of
		// objects -- so ONE rule answers both, which is what makes the suffix a collision marker rather than a
		// decoration applied on the way past.

		template <typename Taken>
		QString available_name ( const QString& base, Taken taken )
		{
			if ( !taken ( base ) )
			{
				return base;
			}

			const QString firstCandidate = base + QStringLiteral ( " (copy)" );

			if ( !taken ( firstCandidate ) )
			{
				return firstCandidate;
			}

			for ( int suffix = 2; ; ++suffix )
			{
				const QString candidate = base + QStringLiteral ( " (copy " ) + QString::number ( suffix ) + QStringLiteral ( ")" );

				if ( !taken ( candidate ) )
				{
					return candidate;
				}
			}
		}

		// Does EVERY element carry this member? A column the array only partly has is ragged rather than present, and
		// pasting onto it is what EDITOR-18's shorter-source rule is about -- so "already carries that name" is asked
		// of the array as a whole.

		bool array_carries_column ( const JsonNode& array, const QString& name )
		{
			const int elementCount = array.array_size ();

			if ( elementCount == 0 )
			{
				return false;
			}

			for ( int index = 0; index < elementCount; ++index )
			{
				const JsonNode* const element = array.array_element ( index );

				if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) || !element->has_member ( name ) )
				{
					return false;
				}
			}

			return true;
		}

		// Does ANY object element carry this member? InsertColumn's collision test: the new column must be new on
		// every element, so one element already holding the name is enough to take it.

		bool array_has_member_anywhere ( const JsonNode& array, const QString& name )
		{
			for ( int index = 0; index < array.array_size (); ++index )
			{
				const JsonNode* const element = array.array_element ( index );

				if ( ( element != nullptr ) && ( element->kind () == JsonKind::Object ) && element->has_member ( name ) )
				{
					return true;
				}
			}

			return false;
		}
	}

	PasteTargetPlan plan_paste_target
	(
		const JsonNode*               target,
		const QString&                listName,
		PasteRoute                    route,
		const std::optional<QString>& targetColumnName
	)
	{
		PasteTargetPlan plan;

		const auto refuse = [ &plan ] ( const QString& reason )
		{
			plan.kind    = PasteTargetKind::Refused;
			plan.refusal = reason;

			return plan;
		};

		if ( target == nullptr )
		{
			return refuse ( QCoreApplication::translate ( "paste_target_plan", "There is nothing selected to paste into." ) );
		}

		// EDIT-16: a scalar is no shape to receive a list of values. Refused rather than falling through to EDIT-03's
		// "insert as the sibling after", which is a different edit wearing this one's gesture.

		if ( target->kind () != JsonKind::Object && target->kind () != JsonKind::Array )
		{
			return refuse
			(
				QCoreApplication::translate
				(
					"paste_target_plan",
					"A list of values cannot be pasted onto a single value. Select an object or an array."
				)
			);
		}

		if ( target->kind () == JsonKind::Object )
		{
			// One member holding an array of the values, keyed `<listName>` where the object has no such member and
			// de-duplicated by EDIT-07's sequence where it does. The suffix is a COLLISION MARKER (15h.2): an object
			// with no `roles` receives `roles`, and only a second paste produces `roles (copy)`.

			const QString base = listName.isEmpty () ? column_naming::UNNAMED : listName;

			plan.kind = PasteTargetKind::Object;
			plan.name = available_name ( base, [ target ] ( const QString& key ) { return target->has_member ( key ); } );

			return plan;
		}

		if ( target->array_size () == 0 )
		{
			// The array BECOMES the list. It has no shape yet, so there is nothing to attach the name to and the
			// values stand as the elements -- which is why this case leaves plan.name empty.

			plan.kind = PasteTargetKind::EmptyArray;

			return plan;
		}

		if ( renders_single_column ( *target ) )
		{
			// THE ROUTE DECIDES, and this is the case that made the route a parameter (15h.2). Both routes arrive here
			// with no target column key -- a single-column table has a name but no member key -- so the presence of
			// one cannot tell them apart, and they now want opposite things.

			if ( route == PasteRoute::Column )
			{
				// Aimed at the column: the values overwrite element by element and extras append as BARE elements. A
				// single-column array holds values rather than named members, and naming them would change its kind.

				plan.kind = PasteTargetKind::OneDimensionalArray;

				return plan;
			}

			// Aimed at the ARRAY: it is reshaped so the pasted column can join it. Every existing bare value moves
			// under the array's own section 2.12 name -- the same rule that named the column on the way out, so a
			// `data` array keeps its values under `data` -- and the pasted values arrive as a new last column.
			//
			// The two names can COLLIDE (pasting a `data` column onto the `data` array), which is the object case's
			// rule reached from a different direction: the incoming column takes the suffix and the existing values
			// keep the plain name, because they were there first.

			plan.kind         = PasteTargetKind::ConvertToArrayOfObjects;
			plan.existingName = column_naming::single_column_name ( *target );

			const QString base = listName.isEmpty () ? column_naming::UNNAMED : listName;

			// An INSERT also has to miss the members an already-object element brings with it -- an array only PARTLY
			// of objects is single-column too, and its object elements keep their members through the reshape.

			const bool inserting = ( route == PasteRoute::InsertColumn );

			plan.name = available_name
			(
				base, [ &plan, target, inserting ] ( const QString& key )
				{
					return ( key == plan.existingName ) || ( inserting && array_has_member_anywhere ( *target, key ) );
				}
			);

			return plan;
		}

		// An array of objects. The TARGET column's name wins on the header route -- the user aimed at a named column,
		// and overwriting it is what aiming at it MEANS.

		plan.kind = PasteTargetKind::ArrayOfObjects;

		if ( ( route == PasteRoute::Column ) && targetColumnName.has_value () )
		{
			plan.name = targetColumnName.value ();

			return plan;
		}

		// From the tree the values arrive under the name they travel with, de-duplicated where the array already
		// carries it -- so a second paste adds `roles (copy)` beside `roles` rather than overwriting it (15h.2).
		// Nothing was aimed at here, so there is no column whose overwriting the user asked for.

		const QString base = listName.isEmpty () ? column_naming::UNNAMED : listName;

		if ( route == PasteRoute::InsertColumn )
		{
			// Taken if ANY element carries it (see PasteRoute): a ragged column is still a column, and the inserted
			// one must not land on the cells it has.

			plan.name = available_name
			(
				base, [ target ] ( const QString& key ) { return array_has_member_anywhere ( *target, key ); }
			);

			return plan;
		}

		plan.name = available_name
		(
			base, [ target ] ( const QString& key ) { return array_carries_column ( *target, key ); }
		);

		return plan;
	}
}

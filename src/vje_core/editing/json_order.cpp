//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_order implementation -- see json_order.hpp for the order EDIT-15 states and why it is not JSONPath's.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/json_order.hpp>

#include <vje_core/document/JsonNode.hpp>

#include <algorithm>
#include <numeric>

namespace vje
{
	namespace
	{
		//=============================================================================================================
		// The kind's position in EDIT-15's cross-kind order. An ABSENT value (a null pointer) ranks with null, which
		// is the ragged-array rule: an element that lacks the column's member sorts as though it carried null.
		//=============================================================================================================

		int kind_rank ( const JsonNode* node )
		{
			if ( node == nullptr )
			{
				return 0;
			}

			switch ( node->kind () )
			{
				case JsonKind::Null:    return 0;
				case JsonKind::Boolean: return 1;
				case JsonKind::Number:  return 2;
				case JsonKind::String:  return 3;
				case JsonKind::Array:   return 4;
				case JsonKind::Object:  return 5;
			}

			return 0;
		}

		//=============================================================================================================
		// The value a single element contributes to the sort. nullopt sorts by the element itself (the single-column
		// scalar array); otherwise it is the named member, which is absent -- a null pointer -- both when the element
		// is not an object at all and when it is one that lacks the key.
		//=============================================================================================================

		const JsonNode* sort_key ( const JsonNode* element, const std::optional<QString>& memberKey )
		{
			if ( ( element == nullptr ) || !memberKey.has_value () )
			{
				return element;
			}

			if ( element->kind () != JsonKind::Object )
			{
				return nullptr;
			}

			return element->find_member ( memberKey.value () );
		}
	}

	namespace json_order
	{
		//=============================================================================================================
		// compare_values
		//=============================================================================================================

		int compare_values ( const JsonNode* left, const JsonNode* right )
		{
			const int leftRank  = kind_rank ( left );
			const int rightRank = kind_rank ( right );

			if ( leftRank != rightRank )
			{
				return ( leftRank < rightRank ) ? -1 : 1;
			}

			// Same rank. Null (and absent) carry no value to compare, and two containers compare EQUAL rather than by
			// their contents -- the stable sort then keeps them in document order, which is the only ordering of two
			// subtrees this application is willing to claim.

			if ( ( left == nullptr ) || ( right == nullptr ) )
			{
				return 0;
			}

			switch ( left->kind () )
			{
				case JsonKind::Boolean:
				{
					// false < true.

					if ( left->boolean_value () == right->boolean_value () ) return  0;

					return left->boolean_value () ? 1 : -1;
				}

				case JsonKind::Number:
				{
					// Compared NUMERICALLY, by value, while the raw tokens stay untouched (FILE-10) -- so 1.50 and
					// 1e3 order as 1.5 and 1000 and are still written back character for character. Two tokens that
					// exceed double's precision and land on the same double compare equal and keep document order,
					// which is the stable sort's answer rather than an arbitrary one.

					const double leftValue  = left ->number_token ().toDouble ();
					const double rightValue = right->number_token ().toDouble ();

					if ( leftValue < rightValue ) return -1;
					if ( leftValue > rightValue ) return  1;

					return 0;
				}

				case JsonKind::String:
				{
					const int ordering = QString::compare ( left->string_value (), right->string_value () );

					if ( ordering < 0 ) return -1;
					if ( ordering > 0 ) return  1;

					return 0;
				}

				case JsonKind::Null:
				case JsonKind::Array:
				case JsonKind::Object:
				{
					return 0;
				}
			}

			return 0;
		}

		//=============================================================================================================
		// sort_permutation
		//=============================================================================================================

		std::vector<int> sort_permutation
		(
			const JsonNode&               array,
			const std::optional<QString>& memberKey,
			Qt::SortOrder                 order
		)
		{
			const int elementCount = array.array_size ();

			std::vector<int> permutation ( static_cast<std::size_t> ( elementCount ) );
			std::iota ( permutation.begin (), permutation.end (), 0 );

			// The keys are gathered ONCE rather than resolved inside the comparator, which is called O ( n log n )
			// times and would otherwise repeat a member lookup per comparison (NFR-03).

			std::vector<const JsonNode*> keys ( static_cast<std::size_t> ( elementCount ) );

			for ( int index = 0; index < elementCount; ++index )
			{
				keys [ static_cast<std::size_t> ( index ) ] = sort_key ( array.array_element ( index ), memberKey );
			}

			const bool ascending = ( order == Qt::AscendingOrder );

			// std::stable_sort, and only the VALUE comparison is negated for a descending sort. Equal elements stay
			// equal under the negated comparison, so both directions leave them in document order (EDIT-15).

			std::stable_sort
			(
				permutation.begin (),
				permutation.end (),
				[ &keys, ascending ] ( int leftIndex, int rightIndex )
				{
					const int ordering = compare_values
					(
						keys [ static_cast<std::size_t> ( leftIndex  ) ],
						keys [ static_cast<std::size_t> ( rightIndex ) ]
					);

					return ascending ? ( ordering < 0 ) : ( ordering > 0 );
				}
			);

			return permutation;
		}

		//=============================================================================================================
		// is_identity
		//=============================================================================================================

		bool is_identity ( const std::vector<int>& permutation )
		{
			for ( std::size_t index = 0; index < permutation.size (); ++index )
			{
				if ( permutation [ index ] != static_cast<int> ( index ) )
				{
					return false;
				}
			}

			return true;
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Validator implementation. See Validator.hpp for the VAL-01..03 rule references.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/Validator.hpp>

#include <vje_core/editing/edit_transforms.hpp>

#include <QHash>

#include <algorithm>

namespace vje
{
	namespace
	{
		// Sorted by key, then by count, so a census is a stable description of a document and the comparison below
		// walks it deterministically.

		bool precedes ( const DuplicateKeyGroup& left, const DuplicateKeyGroup& right )
		{
			return ( left.key == right.key ) ? ( left.count < right.count ) : ( left.key < right.key );
		}

		// How many SURPLUS occurrences of each key the document carries -- an object holding a key three times
		// contributes two, and one holding it once contributes nothing. Summed across every object, because that is
		// what "how duplicated is this key" means for a whole document.

		QHash<QString, int> excess_by_key ( const std::vector<DuplicateKeyGroup>& census )
		{
			QHash<QString, int> excess;

			for ( const DuplicateKeyGroup& group : census )
			{
				excess [ group.key ] += ( group.count - 1 );
			}

			return excess;
		}

		// One entry per ( object, duplicated key ). Recursion is bounded by JsonParser::MAX_DEPTH, which every tree
		// reaching this has already been through.

		void collect_duplicates ( const JsonNode& node, std::vector<DuplicateKeyGroup>& groups )
		{
			if ( node.kind () == JsonKind::Object )
			{
				QHash<QString, int> occurrences;

				for ( int index = 0; index < node.member_count (); ++index )
				{
					++occurrences [ node.member_key ( index ) ];
				}

				for ( auto entry = occurrences.constBegin (); entry != occurrences.constEnd (); ++entry )
				{
					if ( entry.value () > 1 )
					{
						groups.push_back ( DuplicateKeyGroup { entry.key (), entry.value () } );
					}
				}

				for ( int index = 0; index < node.member_count (); ++index )
				{
					if ( const JsonNode* const value = node.member_value ( index ) )
					{
						collect_duplicates ( *value, groups );
					}
				}

				return;
			}

			if ( node.kind () == JsonKind::Array )
			{
				for ( int index = 0; index < node.array_size (); ++index )
				{
					if ( const JsonNode* const element = node.array_element ( index ) )
					{
						collect_duplicates ( *element, groups );
					}
				}
			}
		}
	}
	//=================================================================================================================
	// VAL-01 / VAL-02 -- text validation
	//=================================================================================================================

	ValidationResult Validator::validate ( const QString& text )
	{
		ParseResult parsed = JsonParser::parse ( text );

		ValidationResult result;
		result.ok            = parsed.ok;
		result.issue         = parsed.error;
		result.duplicateKeys = parsed.duplicateKeys;
		result.root          = std::move ( parsed.root );

		return result;
	}

	//=================================================================================================================
	// VAL-02 -- the duplicate-key policy
	//=================================================================================================================

	std::vector<DuplicateKeyGroup> Validator::duplicate_census ( const JsonNode& root )
	{
		std::vector<DuplicateKeyGroup> groups;

		collect_duplicates ( root, groups );

		std::sort ( groups.begin (), groups.end (), precedes );

		return groups;
	}

	std::optional<QString> Validator::introduced_duplicate ( const JsonNode& before, const JsonNode& after )
	{
		const std::vector<DuplicateKeyGroup> census = duplicate_census ( after );

		const QHash<QString, int> had = excess_by_key ( duplicate_census ( before ) );
		const QHash<QString, int> has = excess_by_key ( census );

		// AN INTRODUCTION IS AN INCREASE, per key. Not exact equality and not multiset containment: the first draft
		// required after's ( key, count ) groups to match before's one for one, which reports an INTRODUCTION when a
		// user REMOVES a duplicate -- three "colour" members becoming two matches no group of three, so the commit
		// that repaired the document would have been refused. Found by the neutered-build pass rather than by
		// reading, because every case written at the time only ever added.
		//
		// Summed per key rather than compared group by group, so relocating a duplicated group between objects is
		// not an introduction either -- consistent with the census carrying no position at all.

		for ( const DuplicateKeyGroup& group : census )
		{
			if ( has.value ( group.key ) > had.value ( group.key ) )
			{
				return group.key;                          // Sorted, so the key named is deterministic.
			}
		}

		return std::nullopt;
	}

	bool Validator::introduces_duplicate ( const JsonNode& object, const QString& key, int ignoreIndex )
	{
		if ( object.kind () != JsonKind::Object )
		{
			return false;
		}

		for ( int index = 0; index < object.member_count (); ++index )
		{
			if ( index == ignoreIndex )
			{
				continue;
			}

			if ( object.member_key ( index ) == key )
			{
				return true;
			}
		}

		return false;
	}

	//=================================================================================================================
	// VAL-03 -- Form numeric input
	//=================================================================================================================

	bool Validator::is_valid_number ( const QString& text )
	{
		return edit_transforms::is_json_number ( text.trimmed () );
	}
}

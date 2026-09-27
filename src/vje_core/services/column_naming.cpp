//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   column_naming implementation. See column_naming.hpp for the rule and why it has a file of its own.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/column_naming.hpp>

#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	namespace column_naming
	{
		QString single_column_name ( const JsonNode& array )
		{
			const JsonNode* const parent = array.parent ();

			if ( ( parent != nullptr ) && ( parent->kind () == JsonKind::Object ) )
			{
				const int index = array.index_in_parent ();

				if ( ( index >= 0 ) && ( index < parent->member_count () ) )
				{
					return parent->member_key ( index );
				}
			}

			return UNNAMED;
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   selection_summary implementation -- the node-info pane's three cases. See the header for why the count replaces the
//   type rather than joining it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "selection_summary.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QObject>

namespace vje
{
	namespace
	{
		// One node's type and, for a branch, how much is in it. The middle dot is written as its UTF-8 bytes so the
		// source file's encoding cannot change what is on screen.
		//
		// THE SINGULAR IS ITS OWN STRING rather than a "%n member(s)" source text, and that is a correction this
		// extraction found by MEASUREMENT: with no translator loaded -- which is every build this project ships --
		// QCoreApplication::translate falls back to the source text verbatim and replaces only %n, so the pane read
		// "object - 2 member(s)" against spec section 2.8's own "object - 5 members". Choosing the form in code makes
		// the untranslated build read correctly while still leaving a translator a %n entry for the plural.

		QString describe_node ( const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::Object:
				{
					const int count = node.member_count ();

					return ( count == 1 )
					     ? QObject::tr ( "object \xC2\xB7 1 member" )
					     : QObject::tr ( "object \xC2\xB7 %n members", nullptr, count );
				}

				case JsonKind::Array:
				{
					const int count = node.array_size ();

					return ( count == 1 )
					     ? QObject::tr ( "array \xC2\xB7 1 item" )
					     : QObject::tr ( "array \xC2\xB7 %n items", nullptr, count );
				}

				case JsonKind::String:  return QObject::tr ( "string" );
				case JsonKind::Number:  return QObject::tr ( "number" );
				case JsonKind::Boolean: return QObject::tr ( "boolean" );
				case JsonKind::Null:    return QObject::tr ( "null" );
			}

			return QString ();
		}
	}

	QString selection_summary ( const JsonNode* primaryNode, int selectedCount )
	{
		// TREE-09's count first, and deliberately without consulting the node: what a multi-node command acts on is the
		// SET, so the set's size is the honest answer whether or not the primary happens to resolve.

		if ( selectedCount > 1 )
		{
			// Plural only -- a count of one never reaches here, so there is no singular form to state and none to get
			// wrong. See describe_node above for why the "(s)" spelling is not used.

			return QObject::tr ( "%n nodes selected", nullptr, selectedCount );
		}

		if ( primaryNode == nullptr )
		{
			return QString ();
		}

		return describe_node ( *primaryNode );
	}
}

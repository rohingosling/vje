//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FindController implementation. See FindController.hpp for the snapshot / staleness design and the position rules.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/FindController.hpp"

#include "AppConfig.hpp"
#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/SearchService.hpp>

namespace vje
{
	//=================================================================================================================
	// Free Functions
	//=================================================================================================================

	QString describe_go_to_result ( GoToResult result )
	{
		switch ( result )
		{
			case GoToResult::MalformedPointer:
			{
				// Names the shape rather than the rule, and says how to reach the root -- which is the one destination
				// whose pointer looks like a mistake.

				return QObject::tr ( "Not a JSON Pointer. Use /key/0/name, or leave this empty for the document root." );
			}

			case GoToResult::Unresolvable:
			{
				return QObject::tr ( "No node in this document answers to that pointer." );
			}

			default:
			{
				return QString ();
			}
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FindController::FindController
	(
		JsonDocument*     document,
		SelectionService* selection,
		StatusService*    status,
		ClipboardService* clipboard,
		QObject*          parent
	)
	:	QObject   ( parent ),
		document  ( document ),
		selection ( selection ),
		status    ( status ),
		clipboard ( clipboard )
	{
		// The document is watched unconditionally, but the handlers stand down while the query is empty -- so a session
		// that never opens the find bar pays a null check per edit and nothing more.

		connect ( document, &JsonDocument::node_changed, this, &FindController::handle_document_changed );
		connect ( document, &JsonDocument::reset,        this, &FindController::handle_document_reset );
	}

	//=================================================================================================================
	// Commands -- the query (FIND-01)
	//=================================================================================================================

	bool FindController::set_query ( const QString& text, bool matchCase )
	{
		queryText      = text;
		queryMatchCase = matchCase;

		// Where the user is BEFORE the search runs. This is what makes typing one more character narrow the result set
		// rather than throw the user back to the top of the document.

		const bool        hadAnchor = ( selection != nullptr ) && selection->has_selection ();
		const JsonPointer anchor    = hadAnchor ? selection->selection () : JsonPointer ();

		matches.clear ();

		currentIndex = -1;
		matchesStale = false;

		if ( queryText.isEmpty () || !document->has_root () )
		{
			// Stood down. Not a "no matches" result -- an empty needle is not a failed search, and FIND-03's message
			// would be a false report of one.

			emit results_changed ();

			return false;
		}

		matches = SearchService::find_all ( *document->root (), SearchQuery { queryText, queryMatchCase } );

		if ( matches.empty () )
		{
			// FIND-03: report, and leave the selection exactly where it is.

			if ( status != nullptr )
			{
				status->show_message ( report (), config::find::MESSAGE_TIMEOUT );
			}

			emit results_changed ();

			return false;
		}

		currentIndex = 0;

		if ( hadAnchor )
		{
			for ( std::size_t index = 0; index < matches.size (); ++index )
			{
				if ( matches [ index ] == anchor )
				{
					currentIndex = static_cast<int> ( index );

					break;
				}
			}
		}

		publish_current ();

		emit results_changed ();

		return true;
	}

	//=================================================================================================================
	// Commands -- navigation (FIND-02)
	//=================================================================================================================

	bool FindController::find_next ()
	{
		return step ( 1 );
	}

	bool FindController::find_previous ()
	{
		return step ( -1 );
	}

	//=================================================================================================================
	// Commands -- Go To (FIND-04)
	//=================================================================================================================

	GoToResult FindController::go_to ( const QString& pointerText )
	{
		// The text is NOT trimmed. A JSON Pointer's tokens are literal, and " a " is a legal object key -- trimming
		// would quietly make one destination unreachable in exchange for tolerating a stray space in another.

		bool ok = false;

		const JsonPointer pointer = JsonPointer::parse ( pointerText, &ok );

		if ( !ok )
		{
			return GoToResult::MalformedPointer;
		}

		// Covers the no-document case too: a root pointer against a null root resolves to nothing.

		if ( SearchService::go_to ( document->root (), pointer ) == nullptr )
		{
			return GoToResult::Unresolvable;
		}

		if ( selection != nullptr )
		{
			selection->set_selection ( pointer, SelectionOrigin::GoTo );
		}

		if ( status != nullptr )
		{
			const QString destination = pointer.is_root () ? tr ( "the document root" ) : pointer.to_string ();

			status->show_message ( tr ( "Went to %1" ).arg ( destination ), config::find::MESSAGE_TIMEOUT );
		}

		return GoToResult::Selected;
	}

	//=================================================================================================================
	// Commands -- Copy JSON Pointer (FIND-05)
	//=================================================================================================================

	bool FindController::copy_selection_pointer ()
	{
		if ( ( selection == nullptr ) || !selection->has_selection () )
		{
			return false;
		}

		const JsonPointer pointer = selection->selection ();
		const QString     text    = pointer.to_string ();

		if ( clipboard != nullptr )
		{
			clipboard->set_plain_text ( text );
		}

		if ( status != nullptr )
		{
			// The root's pointer is the empty string, so this one success genuinely leaves the clipboard empty. Reported
			// in its own words rather than as "Copied ", which would read as the command having failed -- the round trip
			// still holds, since an empty Go To field means the root.

			status->show_message
			(
				pointer.is_root () ? tr ( "Copied the document root pointer (empty)" ) : tr ( "Copied %1" ).arg ( text ),
				config::find::MESSAGE_TIMEOUT
			);
		}

		return true;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	const QString& FindController::query () const
	{
		return queryText;
	}

	bool FindController::match_case () const
	{
		return queryMatchCase;
	}

	int FindController::match_count () const
	{
		ensure_matches ();

		return static_cast<int> ( matches.size () );
	}

	int FindController::current_position () const
	{
		ensure_matches ();

		return ( currentIndex >= 0 ) ? ( currentIndex + 1 ) : 0;
	}

	const JsonPointer& FindController::current_match () const
	{
		ensure_matches ();

		if ( ( currentIndex < 0 ) || ( currentIndex >= static_cast<int> ( matches.size () ) ) )
		{
			return emptyPointer;
		}

		return matches [ static_cast<std::size_t> ( currentIndex ) ];
	}

	QString FindController::report () const
	{
		if ( queryText.isEmpty () )
		{
			return QString ();
		}

		const int count = match_count ();

		if ( count == 0 )
		{
			return tr ( "No matches" );
		}

		const int position = current_position ();

		// Matches exist but none is current -- the state a document load leaves behind a live query. Reporting the count
		// alone is the truth; "0 of 17" would read as a broken counter.

		if ( position == 0 )
		{
			return ( count == 1 ) ? tr ( "1 match" ) : tr ( "%1 matches" ).arg ( count );
		}

		// The singular is a separate string rather than an "(es)" suffix: the count is read at a glance while the user
		// is stepping through matches, and "1 of 1 matches" reads as a fault in the count.

		if ( count == 1 )
		{
			return tr ( "%1 of 1 match" ).arg ( position );
		}

		return tr ( "%1 of %2 matches" ).arg ( position ).arg ( count );
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void FindController::handle_document_changed ()
	{
		if ( queryText.isEmpty () )
		{
			return;
		}

		// Marked, not re-run. Whether the walk is worth doing is decided by whether anyone asks -- a dismissed find bar
		// asks nothing, so an ordinary editing session costs this flag per edit (NFR-03).

		matchesStale = true;

		emit results_changed ();
	}

	void FindController::handle_document_reset ()
	{
		// A new root. The previous document's pointers name nothing here, and keeping the position would be a claim
		// about a document that is gone -- so the query survives (F3 still repeats it) and the results do not.

		matches.clear ();

		currentIndex = -1;
		matchesStale = !queryText.isEmpty ();

		emit results_changed ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void FindController::ensure_matches () const
	{
		if ( !matchesStale )
		{
			return;
		}

		matchesStale = false;

		const bool        hadCurrent      = ( currentIndex >= 0 ) && ( currentIndex < static_cast<int> ( matches.size () ) );
		const JsonPointer previousPointer = hadCurrent ? matches [ static_cast<std::size_t> ( currentIndex ) ] : JsonPointer ();

		matches.clear ();

		if ( !queryText.isEmpty () && document->has_root () )
		{
			matches = SearchService::find_all ( *document->root (), SearchQuery { queryText, queryMatchCase } );
		}

		if ( matches.empty () )
		{
			currentIndex = -1;

			return;
		}

		// Keep the match the user was on when it survived the edit -- BY POINTER, because its index will have moved if
		// anything above it was inserted or removed.

		if ( hadCurrent )
		{
			for ( std::size_t index = 0; index < matches.size (); ++index )
			{
				if ( matches [ index ] == previousPointer )
				{
					currentIndex = static_cast<int> ( index );

					return;
				}
			}
		}

		// It did not survive. Clamp into the new list rather than resetting to the top, so the next step continues from
		// roughly where the user had reached.
		//
		// "There was none" is deliberately NOT clamped to the first match: that is the state a document load leaves
		// (handle_document_reset), where the selection is the new root and no match is current -- claiming match 1 there
		// would make the next F3 skip it.

		if ( currentIndex >= static_cast<int> ( matches.size () ) )
		{
			currentIndex = static_cast<int> ( matches.size () ) - 1;
		}
	}

	bool FindController::step ( int direction )
	{
		ensure_matches ();

		if ( matches.empty () )
		{
			// FIND-03. The report is still posted -- the user pressed a key and is owed an answer -- but nothing moves.

			if ( status != nullptr )
			{
				status->show_message ( report (), config::find::MESSAGE_TIMEOUT );
			}

			emit results_changed ();

			return false;
		}

		const int count = static_cast<int> ( matches.size () );

		// One expression for both directions, so the wrap cannot be right at one end and wrong at the other. With no
		// current match, a forward step lands on the first and a backward step on the last.

		currentIndex = ( currentIndex < 0 )
		             ? ( ( direction > 0 ) ? 0 : ( count - 1 ) )
		             : ( ( ( currentIndex + direction ) % count + count ) % count );

		publish_current ();

		emit results_changed ();

		return true;
	}

	void FindController::publish_current ()
	{
		if ( ( currentIndex < 0 ) || ( currentIndex >= static_cast<int> ( matches.size () ) ) )
		{
			return;
		}

		// SelectionOrigin::Find reveals (EDITOR-04), so a match inside a collapsed branch opens the branch. Everything
		// else -- the tree highlight, the presented view, the status bar's node panes -- follows from this one write.

		if ( selection != nullptr )
		{
			selection->set_selection ( matches [ static_cast<std::size_t> ( currentIndex ) ], SelectionOrigin::Find );
		}

		if ( status != nullptr )
		{
			status->show_message ( report (), config::find::MESSAGE_TIMEOUT );
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonDocument implementation. See JsonDocument.hpp for the design notes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>

#include <QHash>
#include <QList>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// mark_differences' walk. Stamps `after`'s nodes where they differ from `before`'s and says whether anything
		// did. Only the differing nodes themselves are stamped here; the caller stamps the ancestor chain once.
		//
		// A node of a different kind is stamped ALONE: its descendants are all new, and a dot on every one of them
		// would say nothing its own dot does not.
		//-------------------------------------------------------------------------------------------------------------

		bool stamp_differences ( const JsonNode& before, JsonNode& after, std::uint64_t epoch )
		{
			bool differs = ( before.kind () != after.kind () );

			if ( !differs )
			{
				switch ( after.kind () )
				{
					case JsonKind::Null:
					{
						break;
					}

					case JsonKind::Boolean:
					{
						differs = ( before.boolean_value () != after.boolean_value () );

						break;
					}

					case JsonKind::Number:
					{
						differs = ( before.number_token () != after.number_token () );   // The token, as FILE-10 keeps it.

						break;
					}

					case JsonKind::String:
					{
						differs = ( before.string_value () != after.string_value () );

						break;
					}

					case JsonKind::Array:
					{
						// By position: the tree's [n] labels are positions, so an element inserted at the front changes
						// what every later label holds, and each of those is honestly a change.

						differs = ( before.array_size () != after.array_size () );

						for ( int index = 0; index < after.array_size (); ++index )
						{
							JsonNode* const child = after.array_element ( index );

							if ( index >= before.array_size () )
							{
								child->set_change_stamp ( epoch );                  // An element the replacement added.

								differs = true;
							}
							else if ( stamp_differences ( *before.array_element ( index ), *child, epoch ) )
							{
								differs = true;
							}
						}

						break;
					}

					case JsonKind::Object:
					{
						// By key, the nth occurrence with the nth (duplicates are legal, FILE-04). A member that only
						// moved is unchanged; its object, whose member order is part of its value, is not.

						differs = ( before.member_count () != after.member_count () );

						QHash<QString, QList<int>> beforePositions;

						for ( int index = 0; index < before.member_count (); ++index )
						{
							beforePositions [ before.member_key ( index ) ].append ( index );

							if ( !differs && ( before.member_key ( index ) != after.member_key ( index ) ) )
							{
								differs = true;
							}
						}

						QHash<QString, int> occurrencesSeen;

						for ( int index = 0; index < after.member_count (); ++index )
						{
							const QString&   key        = after.member_key ( index );
							const QList<int> candidates = beforePositions.value ( key );
							const int        occurrence = occurrencesSeen [ key ]++;
							JsonNode* const  child      = after.member_value ( index );

							if ( occurrence >= candidates.size () )
							{
								child->set_change_stamp ( epoch );                  // A member the replacement added.

								differs = true;
							}
							else if ( stamp_differences ( *before.member_value ( candidates.at ( occurrence ) ), *child, epoch ) )
							{
								differs = true;
							}
						}

						break;
					}
				}
			}

			if ( differs )
			{
				after.set_change_stamp ( epoch );
			}

			return differs;
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonDocument::JsonDocument ( QObject* parent )
		: QObject    ( parent )
		, dirtyState ( false )
	{
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	JsonNode* JsonDocument::root () const
	{
		return rootNode.get ();
	}

	bool JsonDocument::has_root () const
	{
		return rootNode != nullptr;
	}

	const QString& JsonDocument::file_path () const
	{
		return filePath;
	}

	bool JsonDocument::is_dirty () const
	{
		return dirtyState;
	}

	JsonNode* JsonDocument::resolve ( const JsonPointer& pointer ) const
	{
		return pointer.resolve ( rootNode.get () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void JsonDocument::set_root ( std::unique_ptr<JsonNode> newRoot )
	{
		rootNode = std::move ( newRoot );

		// A different document: no mark of the old one's may carry over. Silently, since reset () says it all.

		++changeEpoch;

		changeMarksPresent = false;

		emit reset ();
	}

	std::unique_ptr<JsonNode> JsonDocument::swap_root ( std::unique_ptr<JsonNode> newRoot )
	{
		std::unique_ptr<JsonNode> old = std::move ( rootNode );
		rootNode = std::move ( newRoot );

		return old;
	}

	void JsonDocument::notify_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		// TREE-10 -- marked HERE, ahead of the batch below, so a grouped gesture marks what each command touched and not
		// merely the common ancestor the batch will report. The change is already applied, so the pointer names the
		// node as it now is: the edited scalar, the replacement, or the container whose children changed.

		mark_changed ( resolve ( pointer ) );

		if ( changeBatchDepth > 0 )
		{
			// Collapsed, not dropped: remember the subtree that contains everything the batch has touched so far.

			batchPointer      = batchHasChange ? JsonPointer::common_ancestor ( batchPointer, pointer ) : pointer;
			batchHasChange    = true;

			return;
		}

		emit node_changed ( pointer, change );
	}

	void JsonDocument::begin_change_batch ()
	{
		++changeBatchDepth;
	}

	void JsonDocument::end_change_batch ()
	{
		if ( changeBatchDepth == 0 )
		{
			return;
		}

		if ( --changeBatchDepth > 0 )
		{
			return;                                            // An inner batch never closes the outer one.
		}

		if ( !batchHasChange )
		{
			return;                                            // A group in which nothing was applied says nothing.
		}

		batchHasChange = false;

		// SubtreeReplaced, deliberately: it is the kind every observer treats as "re-derive this subtree", which is
		// exactly what a collapsed batch means and the only claim that is true of an arbitrary set of edits. A finer
		// kind would name a change the batch may not have made.

		emit node_changed ( batchPointer, DocumentChange::SubtreeReplaced );

		batchPointer = JsonPointer ();
	}

	bool JsonDocument::in_change_batch () const
	{
		return changeBatchDepth > 0;
	}

	void JsonDocument::set_file_path ( const QString& path )
	{
		if ( filePath == path )
		{
			return;
		}

		filePath = path;

		emit file_path_changed ( filePath );
	}

	void JsonDocument::set_dirty ( bool dirty )
	{
		// Clean means the document is what is on disk, so nothing in it is an unsaved change (TREE-10). Ahead of the
		// early return, so no mark can outlive a clean state that was already clean.

		if ( !dirty )
		{
			clear_change_marks ();
		}

		if ( dirtyState == dirty )
		{
			return;
		}

		dirtyState = dirty;

		emit dirty_changed ( dirtyState );
	}

	//=================================================================================================================
	// Change marks (TREE-10)
	//=================================================================================================================

	bool JsonDocument::has_change_mark ( const JsonNode* node ) const
	{
		return ( node != nullptr ) && ( node->change_stamp () == changeEpoch );
	}

	bool JsonDocument::has_change_marks () const
	{
		return changeMarksPresent;
	}

	void JsonDocument::mark_changed ( JsonNode* node )
	{
		if ( node == nullptr )
		{
			return;
		}

		// Always to the root, never stopping at the first ancestor already marked. Stopping would be cheaper, and would
		// rest on "a marked node's ancestors are marked" surviving every re-parenting an undo can do; a document is a
		// few dozen levels deep at most, so the whole walk is cheaper than that proof.

		for ( JsonNode* walk = node; walk != nullptr; walk = walk->parent () )
		{
			walk->set_change_stamp ( changeEpoch );
		}

		changeMarksPresent = true;
	}

	bool JsonDocument::mark_differences ( const JsonNode& before, JsonNode& after )
	{
		if ( !stamp_differences ( before, after, changeEpoch ) )
		{
			return false;
		}

		mark_changed ( &after );

		return true;
	}

	void JsonDocument::clear_change_marks ()
	{
		if ( !changeMarksPresent )
		{
			return;
		}

		++changeEpoch;

		changeMarksPresent = false;

		emit change_marks_cleared ();
	}
}

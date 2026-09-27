//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for TREE-10's unsaved-change marks, headlessly, through the real edit path: UndoController
//   pushing the real commands onto a real JsonDocument. What each kind of edit marks, that the marks reach every
//   ancestor, that a save, a load and an undo back to the saved point all clear them, that an undo past the saved point
//   marks again, that a grouped gesture marks each of its edits rather than their common ancestor, and that a
//   replacement marks where it differs and nowhere else.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>
#include <QSignalSpy>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> node_of ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		Q_ASSERT ( result.ok );
		return std::move ( result.root );
	}

	JsonPointer pointer ( const char* text )
	{
		return JsonPointer::parse ( QString::fromUtf8 ( text ) );
	}

	struct Fixture
	{
		JsonDocument   document;
		UndoController controller { &document };

		void load ( const QString& json )
		{
			document.set_root ( node_of ( json ) );
			document.set_dirty ( false );
			controller.clear ();
		}

		// What FileController::save does once the file is written: the document is clean, and the stack's clean
		// point moves to here.

		void save ()
		{
			document.set_dirty ( false );
			controller.set_clean ();
		}

		bool marked ( const char* text ) const
		{
			const JsonNode* const node = document.resolve ( pointer ( text ) );

			Q_ASSERT ( node != nullptr );

			return document.has_change_mark ( node );
		}

		// Every pointer in the document that is marked, in document order -- so a case states the whole answer and an
		// extra mark anywhere fails it, rather than only the marks it thought to ask about.

		QStringList marked_pointers () const
		{
			QStringList result;

			collect ( document.root (), QString (), result );

			return result;
		}

	private:

		void collect ( const JsonNode* node, const QString& path, QStringList& result ) const
		{
			if ( document.has_change_mark ( node ) )
			{
				result.append ( path.isEmpty () ? QStringLiteral ( "(root)" ) : path );
			}

			if ( node->kind () == JsonKind::Object )
			{
				for ( int index = 0; index < node->member_count (); ++index )
				{
					collect ( node->member_value ( index ), path + QLatin1Char ( '/' ) + node->member_key ( index ), result );
				}
			}
			else if ( node->kind () == JsonKind::Array )
			{
				for ( int index = 0; index < node->array_size (); ++index )
				{
					collect ( node->array_element ( index ), path + QLatin1Char ( '/' ) + QString::number ( index ), result );
				}
			}
		}
	};

	const char* const NESTED = R"({"a":{"b":{"c":1},"g":true},"d":2,"e":{"f":"x"}})";
}

class TestChangeMarks : public QObject
{
	Q_OBJECT

private slots:

	void a_loaded_document_has_no_marks ();
	void a_value_edit_marks_the_node_and_every_ancestor ();
	void a_save_clears_every_mark ();
	void an_undo_back_to_the_saved_point_clears_the_marks ();
	void an_undo_past_the_saved_point_marks_again ();
	void an_undo_short_of_the_saved_point_keeps_what_it_marked ();
	void a_load_forgets_the_previous_document_s_marks ();

	void an_added_node_is_marked_with_its_container ();
	void a_delete_marks_the_container ();
	void a_rename_marks_the_member_it_renamed ();
	void a_move_marks_the_node_it_moved ();
	void a_sort_marks_the_elements_whose_position_changed ();

	void a_replacement_marks_where_it_differs_and_nowhere_else ();
	void a_replacement_of_a_different_kind_marks_the_node_alone ();
	void undoing_a_replacement_marks_the_same_places ();

	void a_grouped_gesture_marks_each_edit_not_the_common_ancestor ();
};

//---------------------------------------------------------------------------------------------------------------------
// The lifecycle: marking, and the three ways the marks clear.
//---------------------------------------------------------------------------------------------------------------------

void TestChangeMarks::a_loaded_document_has_no_marks ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QVERIFY ( !fixture.document.has_change_marks () );
	QCOMPARE ( fixture.marked_pointers (), QStringList () );
}

void TestChangeMarks::a_value_edit_marks_the_node_and_every_ancestor ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QCOMPARE ( fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) ), EditOutcome::Applied );

	// The chain up to the root and nothing beside it: /a/g shares a parent with /a/b and was not touched.

	QVERIFY ( fixture.document.has_change_marks () );
	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c" } ) );
}

void TestChangeMarks::a_save_clears_every_mark ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );
	fixture.controller.set_string ( pointer ( "/e/f" ),   QStringLiteral ( "y" ) );

	QSignalSpy cleared ( &fixture.document, &JsonDocument::change_marks_cleared );

	fixture.save ();

	QCOMPARE ( fixture.marked_pointers (), QStringList () );
	QVERIFY  ( !fixture.document.has_change_marks () );

	// ONCE: the save's own set_dirty and the stack's cleanChanged both arrive, and the second finds nothing to clear.
	// A view repaints on this signal, so a second emission is a second whole-tree repaint for nothing.

	QCOMPARE ( cleared.count (), 1 );
}

void TestChangeMarks::an_undo_back_to_the_saved_point_clears_the_marks ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );
	fixture.controller.set_number ( pointer ( "/d" ),     QStringLiteral ( "7" ) );

	fixture.controller.undo ();

	QVERIFY ( fixture.document.is_dirty () );
	QVERIFY ( fixture.document.has_change_marks () );

	fixture.controller.undo ();

	// Back at the loaded state, which is the saved point: the stack is clean, and so is the tree.

	QVERIFY  ( !fixture.document.is_dirty () );
	QCOMPARE ( fixture.marked_pointers (), QStringList () );

	// And a redo AWAY from it marks again, from what the redo re-applied.

	fixture.controller.redo ();

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c" } ) );
}

void TestChangeMarks::an_undo_past_the_saved_point_marks_again ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );

	fixture.save ();

	QCOMPARE ( fixture.marked_pointers (), QStringList () );

	// The file now says 5. Undoing puts 1 back, which differs from the file again -- so the marks return, which is the
	// honest answer.

	fixture.controller.undo ();

	QVERIFY  ( fixture.document.is_dirty () );
	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c" } ) );

	// And redoing arrives back at the saved point, which clears them.

	fixture.controller.redo ();

	QCOMPARE ( fixture.marked_pointers (), QStringList () );
}

void TestChangeMarks::an_undo_short_of_the_saved_point_keeps_what_it_marked ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );
	fixture.controller.set_number ( pointer ( "/d" ),     QStringLiteral ( "7" ) );

	fixture.controller.undo ();

	// ACCUMULATED, NOT DIFFED (TREE-10). /d is back to what the file holds, but the marker records where the document
	// has been edited since it was saved; answering "does /d differ from the file?" would need a second copy of the
	// document. The undo's own change marks /d again, and nothing takes a mark away short of a clean state.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c", "/d" } ) );
}

void TestChangeMarks::a_load_forgets_the_previous_document_s_marks ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );

	// set_root alone, without the set_dirty ( false ) a real load also makes -- the new document must arrive unmarked
	// on the strength of the load itself.

	fixture.document.set_root ( node_of ( QString::fromUtf8 ( NESTED ) ) );

	QVERIFY  ( !fixture.document.has_change_marks () );
	QCOMPARE ( fixture.marked_pointers (), QStringList () );

	// A parsed document's nodes arrive unstamped, so the case above would pass however set_root treated the marks. The
	// guarantee is that NO node arrives marked -- including one stamped before, which is what re-installing a marked
	// tree puts in front of it.

	fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "6" ) );

	// Not by clearing the undo stack first: a cleared stack is a clean one, and a clean document has no marks to carry.

	std::unique_ptr<JsonNode> markedTree = fixture.document.swap_root ( nullptr );

	QVERIFY ( fixture.document.has_change_mark ( markedTree.get () ) );

	fixture.document.set_root ( std::move ( markedTree ) );

	QVERIFY  ( !fixture.document.has_change_marks () );
	QCOMPARE ( fixture.marked_pointers (), QStringList () );
}

//---------------------------------------------------------------------------------------------------------------------
// What each structural edit marks: the container its pointer names, and the child it knows it touched.
//---------------------------------------------------------------------------------------------------------------------

void TestChangeMarks::an_added_node_is_marked_with_its_container ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QCOMPARE ( fixture.controller.add_child ( pointer ( "/e" ), JsonKind::Number, QStringLiteral ( "n" ) ), EditOutcome::Applied );

	// The new member itself, not only the object it went into -- the notification names only the object.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/e", "/e/n" } ) );
}

void TestChangeMarks::a_delete_marks_the_container ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QCOMPARE ( fixture.controller.delete_node ( pointer ( "/a/g" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a" } ) );
}

void TestChangeMarks::a_rename_marks_the_member_it_renamed ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QCOMPARE ( fixture.controller.rename_key ( pointer ( "/a/g" ), QStringLiteral ( "h" ) ), EditOutcome::Applied );

	// The renamed member (now /a/h) and not its sibling: KeyRenamed names the object, so without the command's own
	// mark the member whose name changed would be the one row with no dot.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/h" } ) );
}

void TestChangeMarks::a_move_marks_the_node_it_moved ()
{
	Fixture fixture;

	fixture.load ( QStringLiteral ( R"({"list":[10,20,30]})" ) );

	QCOMPARE ( fixture.controller.move_node ( pointer ( "/list/2" ), MoveDirection::Up ), EditOutcome::Applied );

	// 30 moved to [1]; 20, pushed down to [2], is marked by nothing but its container. The command knows which child
	// it MOVED, and that is the one it marks.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/list", "/list/1" } ) );
}

void TestChangeMarks::a_sort_marks_the_elements_whose_position_changed ()
{
	Fixture fixture;

	fixture.load ( QStringLiteral ( R"({"list":[1,3,2,4]})" ) );

	QCOMPARE ( fixture.controller.sort_array ( pointer ( "/list" ), std::nullopt, Qt::AscendingOrder ), EditOutcome::Applied );

	// [1,2,3,4]: the 1 and the 4 stayed where they were, and [1] and [2] now hold something else.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/list", "/list/1", "/list/2" } ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Replacements -- a Code View commit, a type change, an array transform -- mark where they differ.
//---------------------------------------------------------------------------------------------------------------------

void TestChangeMarks::a_replacement_marks_where_it_differs_and_nowhere_else ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	// A Code View commit's shape: the whole document replaced, with one value changed, one member added, and /e's
	// member moved in front of /d -- which changes the root's member order but not /e itself.

	const QString edited = QStringLiteral ( R"({"a":{"b":{"c":9},"g":true},"e":{"f":"x"},"d":2,"z":null})" );

	QCOMPARE ( fixture.controller.replace_subtree ( JsonPointer (), node_of ( edited ), QStringLiteral ( "Commit" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c", "/z" } ) );
}

void TestChangeMarks::a_replacement_of_a_different_kind_marks_the_node_alone ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	QCOMPARE ( fixture.controller.replace_subtree ( pointer ( "/d" ), node_of ( QStringLiteral ( R"({"p":1,"q":2})" ) ), QStringLiteral ( "Replace" ) ), EditOutcome::Applied );

	// The number became an object. Its members are all new, and a dot on each would say nothing /d's dot does not.

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/d" } ) );
}

void TestChangeMarks::undoing_a_replacement_marks_the_same_places ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	fixture.controller.set_number ( pointer ( "/d" ), QStringLiteral ( "3" ) );

	fixture.save ();

	const QString edited = QStringLiteral ( R"({"a":{"b":{"c":9},"g":true},"d":3,"e":{"f":"x"}})" );

	fixture.controller.replace_subtree ( JsonPointer (), node_of ( edited ), QStringLiteral ( "Commit" ) );

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c" } ) );

	// Undo reinstalls the old root. Back at the saved point, so clean -- which is why the save above exists: without
	// it the undo would clean the marks and the symmetry would go unobserved.

	fixture.controller.undo ();

	QVERIFY  ( !fixture.document.is_dirty () );
	QCOMPARE ( fixture.marked_pointers (), QStringList () );

	// Undo once more, past the saved point: the old /d edit, marked from the reinstalled root.

	fixture.controller.undo ();

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/d" } ) );

	// Redo the /d edit, which arrives back at the saved point and clears; then the replacement, compared the same way
	// round as the first time -- so exactly its first marks, and /d is not among them.

	fixture.controller.redo ();

	QCOMPARE ( fixture.marked_pointers (), QStringList () );

	fixture.controller.redo ();

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c" } ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Batching.
//---------------------------------------------------------------------------------------------------------------------

void TestChangeMarks::a_grouped_gesture_marks_each_edit_not_the_common_ancestor ()
{
	Fixture fixture;

	fixture.load ( QString::fromUtf8 ( NESTED ) );

	// Recorded by a lambda rather than a QSignalSpy, which would need JsonPointer registered as a metatype.

	QList<JsonPointer> changes;

	QObject::connect ( &fixture.document, &JsonDocument::node_changed, &fixture.document, [ &changes ] ( const JsonPointer& changed, DocumentChange )
	{
		changes.append ( changed );
	} );

	{
		UndoController::MacroScope scope ( fixture.controller, QStringLiteral ( "Two edits" ) );

		fixture.controller.set_number ( pointer ( "/a/b/c" ), QStringLiteral ( "5" ) );
		fixture.controller.set_string ( pointer ( "/e/f" ),   QStringLiteral ( "y" ) );
	}

	// The batch reported ONE change, naming the root -- the two edits' common ancestor. Marking from that would have
	// marked the root alone; the marks are made as each edit arrives, before the batch collapses them.

	QCOMPARE ( changes.count (), 1 );
	QVERIFY  ( changes.first ().is_root () );

	QCOMPARE ( fixture.marked_pointers (), QStringList ( { "(root)", "/a", "/a/b", "/a/b/c", "/e", "/e/f" } ) );
}

QTEST_GUILESS_MAIN ( TestChangeMarks )

#include "tst_change_marks.moc"

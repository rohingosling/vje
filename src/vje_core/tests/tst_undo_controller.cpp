//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for UndoController's stack semantics and edit-outcome contract: multi-level undo/redo (UNDO-01/02),
//   the redo stack clearing on a new edit (UNDO-03), the dirty flag tracking the clean state (UNDO-04), a single undo
//   step for an array transform (UNDO-02), and the EditOutcome results -- including the VAL-02 duplicate-key and
//   VAL-03 invalid-number rejections and the various no-op (Unchanged) cases -- none of which leave a stack entry.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>

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

	struct Fixture
	{
		JsonDocument   document;
		UndoController controller { &document };

		void load ( const QString& json )
		{
			document.set_root ( node_of ( json ) );
			controller.clear ();
		}

		QString text () const
		{
			return JsonSerializer::serialize ( *document.root () );
		}
	};
}

class TestUndoController : public QObject
{
	Q_OBJECT

private slots:

	void dirty_tracks_the_clean_state ();
	void set_clean_rebaselines_the_dirty_flag ();
	void multi_level_undo_and_redo ();
	void new_edit_clears_the_redo_stack ();
	void array_transform_is_one_undo_step ();

	void rejects_duplicate_key_on_rename ();
	void rejects_duplicate_key_on_add ();
	void rejects_invalid_number ();
	void rejects_value_edit_on_wrong_kind ();
	void rejects_deleting_and_duplicating_the_root ();
	void no_op_edits_report_unchanged_and_push_nothing ();

	// Placement inserts and node paste (Phase 9 -- EDITOR-11 / 12, EDIT-06).

	void insert_element_at_places_a_specific_node ();
	void insert_member_at_places_a_specific_node_and_rejects_a_duplicate ();
	void append_element_adds_at_the_end ();
	void paste_node_places_relative_to_the_selection ();
	void paste_node_synthesizes_and_dedupes_an_object_key ();
	void paste_node_rejects_a_root_scalar ();
};

//---------------------------------------------------------------------------------------------------------------------
// Stack / dirty semantics (UNDO-01..04)
//---------------------------------------------------------------------------------------------------------------------

void TestUndoController::dirty_tracks_the_clean_state ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	QSignalSpy dirtySpy ( &fixture.document, &JsonDocument::dirty_changed );

	QVERIFY ( !fixture.document.is_dirty () );
	QVERIFY ( fixture.controller.is_clean () );

	fixture.controller.set_number ( JsonPointer::parse ( "/a" ), QStringLiteral ( "2" ) );

	QVERIFY ( fixture.document.is_dirty () );
	QVERIFY ( !fixture.controller.is_clean () );

	// Undoing back to the saved baseline clears the modified indicator (UNDO-04).

	fixture.controller.undo ();

	QVERIFY ( !fixture.document.is_dirty () );
	QVERIFY ( fixture.controller.is_clean () );

	// The flag toggled true then false -- two change signals.

	QCOMPARE ( dirtySpy.count (), 2 );
}

void TestUndoController::set_clean_rebaselines_the_dirty_flag ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	fixture.controller.set_number ( JsonPointer::parse ( "/a" ), QStringLiteral ( "2" ) );
	QVERIFY ( fixture.document.is_dirty () );

	// A save marks the current (edited) state as the new clean baseline.

	fixture.controller.set_clean ();

	QVERIFY ( !fixture.document.is_dirty () );
	QVERIFY ( fixture.controller.is_clean () );
}

void TestUndoController::multi_level_undo_and_redo ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2,"c":3})" );

	const QString original = fixture.text ();

	fixture.controller.set_number ( JsonPointer::parse ( "/a" ), QStringLiteral ( "10" ) );
	fixture.controller.set_number ( JsonPointer::parse ( "/b" ), QStringLiteral ( "20" ) );
	fixture.controller.set_number ( JsonPointer::parse ( "/c" ), QStringLiteral ( "30" ) );

	const QString edited = fixture.text ();
	QCOMPARE ( edited, QStringLiteral ( R"({"a":10,"b":20,"c":30})" ) );

	fixture.controller.undo ();
	fixture.controller.undo ();
	fixture.controller.undo ();
	QVERIFY  ( !fixture.controller.can_undo () );
	QCOMPARE ( fixture.text (), original );

	fixture.controller.redo ();
	fixture.controller.redo ();
	fixture.controller.redo ();
	QVERIFY  ( !fixture.controller.can_redo () );
	QCOMPARE ( fixture.text (), edited );
}

void TestUndoController::new_edit_clears_the_redo_stack ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2})" );

	fixture.controller.set_number ( JsonPointer::parse ( "/a" ), QStringLiteral ( "10" ) );
	fixture.controller.undo ();
	QVERIFY ( fixture.controller.can_redo () );

	// A fresh edit after an undo drops the redo branch (UNDO-03, linear history).

	fixture.controller.set_number ( JsonPointer::parse ( "/b" ), QStringLiteral ( "20" ) );

	QVERIFY  ( !fixture.controller.can_redo () );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"a":1,"b":20})" ) );
}

void TestUndoController::array_transform_is_one_undo_step ()
{
	Fixture fixture;
	fixture.load ( R"([{"a":1},{"b":2},{"c":3}])" );

	const QString original = fixture.text ();

	fixture.controller.normalize_array ( JsonPointer () );
	QVERIFY ( fixture.text () != original );

	// A single undo restores the whole transform (UNDO-02).

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), original );
	QVERIFY  ( !fixture.controller.can_undo () );
}

//---------------------------------------------------------------------------------------------------------------------
// Edit-outcome contract
//---------------------------------------------------------------------------------------------------------------------

void TestUndoController::rejects_duplicate_key_on_rename ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.rename_key ( JsonPointer::parse ( "/a" ), QStringLiteral ( "b" ) );

	QCOMPARE ( outcome, EditOutcome::Rejected );              // VAL-02.
	QCOMPARE ( fixture.text (), before );
	QVERIFY  ( !fixture.controller.can_undo () );             // Nothing pushed.
}

void TestUndoController::rejects_duplicate_key_on_add ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.add_child ( JsonPointer (), JsonKind::Number, QStringLiteral ( "a" ) );

	QCOMPARE ( outcome, EditOutcome::Rejected );              // VAL-02.
	QCOMPARE ( fixture.text (), before );
	QVERIFY  ( !fixture.controller.can_undo () );
}

void TestUndoController::rejects_invalid_number ()
{
	Fixture fixture;
	fixture.load ( R"({"n":1})" );

	const QString before = fixture.text ();

	// "01" has a leading zero -- not an RFC 8259 number (VAL-03).

	const EditOutcome outcome = fixture.controller.set_number ( JsonPointer::parse ( "/n" ), QStringLiteral ( "01" ) );

	QCOMPARE ( outcome, EditOutcome::Rejected );
	QCOMPARE ( fixture.text (), before );
	QVERIFY  ( !fixture.controller.can_undo () );
}

void TestUndoController::rejects_value_edit_on_wrong_kind ()
{
	Fixture fixture;
	fixture.load ( R"({"n":1})" );

	// /n is a number; a string set does not apply (that would be a type change, not a value edit).

	const EditOutcome outcome = fixture.controller.set_string ( JsonPointer::parse ( "/n" ), QStringLiteral ( "x" ) );

	QCOMPARE ( outcome, EditOutcome::Rejected );
	QVERIFY  ( !fixture.controller.can_undo () );
}

void TestUndoController::rejects_deleting_and_duplicating_the_root ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	QCOMPARE ( fixture.controller.delete_node    ( JsonPointer () ), EditOutcome::Rejected );
	QCOMPARE ( fixture.controller.duplicate_node ( JsonPointer () ), EditOutcome::Rejected );
	QVERIFY  ( !fixture.controller.can_undo () );
}

void TestUndoController::no_op_edits_report_unchanged_and_push_nothing ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"s":"x","arr":[1,2]})" );

	// Rename to the same key, set to the current value, convert to the current type, and move at the edge are all
	// no-ops: Unchanged, and nothing on the undo stack.

	QCOMPARE ( fixture.controller.rename_key  ( JsonPointer::parse ( "/a" ), QStringLiteral ( "a" ) ),   EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.set_string  ( JsonPointer::parse ( "/s" ), QStringLiteral ( "x" ) ),   EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.change_type ( JsonPointer::parse ( "/a" ), JsonKind::Number ),         EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.move_node   ( JsonPointer::parse ( "/arr/0" ), MoveDirection::Up ),    EditOutcome::Unchanged );

	QVERIFY ( !fixture.controller.can_undo () );
	QVERIFY ( !fixture.document.is_dirty () );
}

//---------------------------------------------------------------------------------------------------------------------
// Placement inserts and node paste (Phase 9)
//---------------------------------------------------------------------------------------------------------------------

void TestUndoController::insert_element_at_places_a_specific_node ()
{
	Fixture fixture;
	fixture.load ( R"([ 1, 3 ])" );

	QCOMPARE ( fixture.controller.insert_element_at ( JsonPointer (), 1, JsonNode::make_number ( QStringLiteral ( "2" ) ), QStringLiteral ( "Insert" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "[1,2,3]" ) );

	// Out of range is rejected without touching the document.

	QCOMPARE ( fixture.controller.insert_element_at ( JsonPointer (), 9, JsonNode::make_null (), QStringLiteral ( "Insert" ) ), EditOutcome::Rejected );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( "[1,3]" ) );
}

void TestUndoController::insert_member_at_places_a_specific_node_and_rejects_a_duplicate ()
{
	Fixture fixture;
	fixture.load ( R"({ "a": 1, "c": 3 })" );

	QCOMPARE ( fixture.controller.insert_member_at ( JsonPointer (), 1, QStringLiteral ( "b" ), JsonNode::make_number ( QStringLiteral ( "2" ) ), QStringLiteral ( "Insert" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"a":1,"b":2,"c":3})" ) );

	// VAL-02: a duplicate key is rejected.

	QCOMPARE ( fixture.controller.insert_member_at ( JsonPointer (), 0, QStringLiteral ( "a" ), JsonNode::make_null (), QStringLiteral ( "Insert" ) ), EditOutcome::Rejected );
}

void TestUndoController::append_element_adds_at_the_end ()
{
	Fixture fixture;
	fixture.load ( R"([ "a" ])" );

	QCOMPARE ( fixture.controller.append_element ( JsonPointer (), JsonNode::make_string ( QStringLiteral ( "b" ) ), QStringLiteral ( "Append" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( R"(["a","b"])" ) );
}

void TestUndoController::paste_node_places_relative_to_the_selection ()
{
	// EDIT-06 placement: a container receives the paste as its last child; a scalar as the sibling after itself.

	Fixture fixture;
	fixture.load ( R"({ "items": [ 1, 2 ], "name": "x" })" );

	// Into the array container -> appended.

	QCOMPARE ( fixture.controller.paste_node ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), JsonNode::make_number ( QStringLiteral ( "3" ) ) ), EditOutcome::Applied );
	QCOMPARE ( fixture.document.resolve ( JsonPointer::parse ( QStringLiteral ( "/items/2" ) ) )->number_token (), QStringLiteral ( "3" ) );

	// After an array element -> inserted as the following sibling.

	QCOMPARE ( fixture.controller.paste_node ( JsonPointer::parse ( QStringLiteral ( "/items/0" ) ), JsonNode::make_number ( QStringLiteral ( "9" ) ) ), EditOutcome::Applied );
	QCOMPARE ( fixture.document.resolve ( JsonPointer::parse ( QStringLiteral ( "/items/1" ) ) )->number_token (), QStringLiteral ( "9" ) );
}

void TestUndoController::paste_node_synthesizes_and_dedupes_an_object_key ()
{
	Fixture fixture;
	fixture.load ( R"({ "email": "a" })" );

	// A carried source key is reused; a collision is de-duplicated rather than refused (EDIT-06).

	QCOMPARE ( fixture.controller.paste_node ( JsonPointer (), JsonNode::make_string ( QStringLiteral ( "b" ) ), QStringLiteral ( "email" ) ), EditOutcome::Applied );

	JsonNode* const root = fixture.document.root ();

	QVERIFY ( root->has_member ( QStringLiteral ( "email" ) ) );
	QVERIFY ( root->has_member ( QStringLiteral ( "email (copy)" ) ) );

	// No carried key -> a synthesized one.

	QCOMPARE ( fixture.controller.paste_node ( JsonPointer (), JsonNode::make_null (), QString () ), EditOutcome::Applied );
	QVERIFY  ( root->has_member ( QStringLiteral ( "item" ) ) );
}

void TestUndoController::paste_node_rejects_a_root_scalar ()
{
	// A lone scalar root has no container to receive the paste and no sibling slot.

	Fixture fixture;
	fixture.load ( R"("just a string")" );

	QCOMPARE ( fixture.controller.paste_node ( JsonPointer (), JsonNode::make_null () ), EditOutcome::Rejected );
}

QTEST_GUILESS_MAIN ( TestUndoController )

#include "tst_undo_controller.moc"

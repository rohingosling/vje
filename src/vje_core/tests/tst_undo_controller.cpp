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

	void the_duplicate_key_policy_governs_both_guards ();
	void the_duplicate_key_policy_never_governs_naming ();

	void rejects_duplicate_key_on_rename ();
	void rejects_duplicate_key_on_add ();
	void rejects_invalid_number ();
	void rejects_value_edit_on_wrong_kind ();
	void rejects_deleting_and_duplicating_the_root ();
	void no_op_edits_report_unchanged_and_push_nothing ();
	void normalizing_an_already_uniform_array_is_unchanged ();
	void replacing_a_subtree_with_an_equal_one_is_unchanged ();
	void a_transform_that_does_change_the_document_still_applies ();

	// Placement inserts and node paste (Phase 9 -- EDITOR-11 / 12, EDIT-06).

	void insert_element_at_places_a_specific_node ();
	void insert_member_at_places_a_specific_node_and_rejects_a_duplicate ();
	void append_element_adds_at_the_end ();
	void paste_node_places_relative_to_the_selection ();
	void paste_node_synthesizes_and_dedupes_an_object_key ();
	void paste_node_rejects_a_root_scalar ();

	// The reorder primitive and undo grouping (Phase 15f -- EDIT-10 / EDIT-14).

	void move_child_reorders_within_a_container ();
	void move_child_moves_an_object_member_with_its_key ();
	void move_child_rejects_a_bad_target_and_reports_a_stationary_move_unchanged ();
	void a_macro_scope_makes_several_edits_one_undo_step ();
	void a_macro_scope_that_pushed_nothing_leaves_the_stack_untouched ();
	void a_macro_scope_reports_whether_anything_reached_the_stack ();

	// Change batching (NFR-03).

	void a_grouped_gesture_notifies_once_naming_the_common_ancestor ();
	void an_ungrouped_edit_still_notifies_with_its_own_change_kind ();
	void a_group_that_applied_nothing_notifies_nothing ();
	void undoing_and_redoing_a_group_notifies_once_each ();
	void every_edit_kind_is_grouped_by_a_live_macro_scope ();
	void a_nested_macro_scope_leaves_the_group_to_the_outer_one ();
	void delete_nodes_removes_every_element_it_was_given ();
	void delete_nodes_is_one_undo_step_whatever_it_removed ();
	void delete_nodes_removes_object_members_by_key ();
	void delete_nodes_skips_what_it_cannot_remove ();
	void repeated_pastes_append_in_order_and_insert_in_reverse ();
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

// THE MOTIVATING CASE FOR VAL-04 ACROSS THE EDIT COMMANDS (Phase 15, 2026-08-02).
//
// normalize_array_elements is idempotent (edit_transforms.hpp), so normalizing an array whose elements already carry
// the same members rebuilds an EQUAL subtree. replace_subtree used to push that anyway and report Applied: the
// document went dirty, the undo stack grew by a step that undid nothing visible, and the command said nothing at all.
// A user could not tell that from a command that was simply broken -- which is what made this a VAL-04 defect rather
// than a cosmetic one.
//
// The dirty flag is the half that makes it more than a wording problem: a spurious Applied means the FILE-08 gate
// asks whether to save a document that has not changed.

void TestUndoController::normalizing_an_already_uniform_array_is_unchanged ()
{
	Fixture fixture;
	fixture.load ( R"({"rows":[{"a":1,"b":2},{"a":3,"b":4}]})" );

	QCOMPARE ( fixture.controller.normalize_array ( JsonPointer::parse ( "/rows" ) ), EditOutcome::Unchanged );

	QVERIFY ( !fixture.controller.can_undo () );
	QVERIFY ( !fixture.document.is_dirty () );
}

// The same rule reached through the primitive itself, which is where it is implemented and therefore where a Code
// View commit with no textual change and a table cell re-set to its own value both arrive. CodeView has branched on
// Unchanged since Phase 8 ("No changes to apply.") -- an outcome replace_subtree could not produce until now.

void TestUndoController::replacing_a_subtree_with_an_equal_one_is_unchanged ()
{
	Fixture fixture;
	fixture.load ( R"({"a":{"b":[1,2,3]}})" );

	// Replaced with a clone of itself -- an equal subtree by construction, and a deep one, so this exercises
	// JsonNode::equals rather than an identity check that would pass for the wrong reason.

	JsonNode* const target = fixture.document.resolve ( JsonPointer::parse ( "/a" ) );

	QVERIFY ( target != nullptr );

	QCOMPARE
	(
		fixture.controller.replace_subtree
		(
			JsonPointer::parse ( "/a" ),
			target->clone (),
			QStringLiteral ( "Replace" )
		),
		EditOutcome::Unchanged
	);

	QVERIFY ( !fixture.controller.can_undo () );
	QVERIFY ( !fixture.document.is_dirty () );
}

// The control. A short-circuit that fired too eagerly would break every transform at once and pass the two cases
// above while doing so, so the arm that MUST still apply is asserted beside them -- a ragged array is exactly the
// input Normalize exists for.

void TestUndoController::a_transform_that_does_change_the_document_still_applies ()
{
	Fixture fixture;
	fixture.load ( R"({"rows":[{"a":1},{"b":2}]})" );

	QCOMPARE ( fixture.controller.normalize_array ( JsonPointer::parse ( "/rows" ) ), EditOutcome::Applied );

	QVERIFY ( fixture.controller.can_undo () );
	QVERIFY ( fixture.document.is_dirty () );
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

//---------------------------------------------------------------------------------------------------------------------
// Reorder and grouping (Phase 15f -- EDIT-10 / EDIT-14)
//---------------------------------------------------------------------------------------------------------------------

void TestUndoController::move_child_reorders_within_a_container ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30,40]})" );

	const JsonPointer items = JsonPointer::parse ( "/items" );

	// A MIDDLE element to a MIDDLE position, deliberately: a move to or from an edge can be satisfied by clamping,
	// and would pass against an implementation that had lost the second index altogether (the D10 trap Phase 12's
	// arrow-key case walked into).

	QCOMPARE ( fixture.controller.move_child ( items, 2, 1 ), EditOutcome::Applied );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,30,20,40]})" ) );

	// One command, and its undo is the exact inverse rather than an approximation of it.

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30,40]})" ) );
}

void TestUndoController::move_child_moves_an_object_member_with_its_key ()
{
	// The half that would fail silently if move_child took only the value: an object's keys live in a parallel list,
	// so a move that carried the value alone would leave every key from that point on attached to the wrong member.

	Fixture fixture;
	fixture.load ( R"({"root":{"a":1,"b":2,"c":3}})" );

	QCOMPARE ( fixture.controller.move_child ( JsonPointer::parse ( "/root" ), 0, 2 ), EditOutcome::Applied );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"root":{"b":2,"c":3,"a":1}})" ) );
}

void TestUndoController::move_child_rejects_a_bad_target_and_reports_a_stationary_move_unchanged ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30],"scalar":1})" );

	const JsonPointer items = JsonPointer::parse ( "/items" );

	QCOMPARE ( fixture.controller.move_child ( JsonPointer::parse ( "/missing" ), 0, 1 ), EditOutcome::Rejected );
	QCOMPARE ( fixture.controller.move_child ( JsonPointer::parse ( "/scalar" ), 0, 0 ), EditOutcome::Rejected );

	// Both indices name an EXISTING child, unlike the insert primitives whose upper bound is one past the end: a move
	// does not change the container's size, so there is no slot at count to move into.

	QCOMPARE ( fixture.controller.move_child ( items,  0,  3 ), EditOutcome::Rejected );
	QCOMPARE ( fixture.controller.move_child ( items,  3,  0 ), EditOutcome::Rejected );
	QCOMPARE ( fixture.controller.move_child ( items, -1,  0 ), EditOutcome::Rejected );

	// EDIT-10's no-op: a drop that landed where the node already was. Unchanged rather than Applied, so nothing is
	// pushed, the document does not go dirty, and the caller has something true to report.

	QCOMPARE ( fixture.controller.move_child ( items, 1, 1 ), EditOutcome::Unchanged );

	QVERIFY  ( !fixture.controller.can_undo () );
	QVERIFY  ( !fixture.document.is_dirty () );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30],"scalar":1})" ) );
}

void TestUndoController::a_macro_scope_makes_several_edits_one_undo_step ()
{
	// EDIT-14's central claim, and the one a user checks by pressing Ctrl+Z once: four deletes are ONE step.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30,40]})" );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Delete Nodes" ) );

		for ( int index = 3; index >= 0; --index )
		{
			fixture.controller.delete_node ( JsonPointer::parse ( QStringLiteral ( "/items/%1" ).arg ( index ) ) );
		}
	}

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[]})" ) );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30,40]})" ) );
	QVERIFY2 ( !fixture.controller.can_undo (), "four deletes inside one scope must leave exactly one undo step" );

	// And redo puts the whole group back, which is the half that would break if the macro were closed per edit.

	fixture.controller.redo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[]})" ) );
	QVERIFY  ( !fixture.controller.can_redo () );
}

void TestUndoController::a_macro_scope_that_pushed_nothing_leaves_the_stack_untouched ()
{
	// The reason the macro is opened LAZILY. QUndoStack::beginMacro pushes its parent command whether or not anything
	// is ever added to it, so a scope whose every edit turned out to be Rejected or Unchanged would leave an undo step
	// that undoes nothing -- and would dirty the document on the way, which is the defect Phase 15 found in
	// replace_subtree (lesson D19) arriving by a different route.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30]})" );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Move Nodes" ) );

		QCOMPARE ( fixture.controller.move_child ( JsonPointer::parse ( "/items" ), 1, 1 ), EditOutcome::Unchanged );
		QCOMPARE ( fixture.controller.delete_node ( JsonPointer () ), EditOutcome::Rejected );
	}

	QVERIFY2 ( !fixture.controller.can_undo (), "an empty group must leave no undo step behind" );
	QVERIFY2 ( !fixture.document.is_dirty (), "an empty group must not dirty the document" );
	QVERIFY  ( fixture.controller.is_clean () );
}

void TestUndoController::every_edit_kind_is_grouped_by_a_live_macro_scope ()
{
	// THE ESCAPE THIS EXISTS TO CATCH. push_command's own comment says a second push site would silently escape the
	// grouping -- and replace_subtree was one, from Phase 9 until Phase 15h found it. Nothing noticed for six phases,
	// because until EDITOR-18's column paste no caller had ever grouped several replacements: a lone replacement
	// outside a macro looks identical either way.
	//
	// So the claim is made over the WHOLE surface rather than over the one call that was wrong. Every edit inside one
	// scope must collapse to one step, which is a property of push_command being the only door rather than of any
	// particular command remembering to use it.
	//
	// THE BYPASSING CALL GOES FIRST, and that ordering is the whole case. The macro is opened LAZILY by the first
	// command that reaches push_command -- so once it is open, a later undoStack.push lands inside it anyway and the
	// escape is invisible. Only the call that WOULD have opened the group can be seen to have skipped it. Written
	// with replace_subtree third, this case passed against the very build it was written for.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30],"name":"a","flag":false,"box":{"k":1}})" );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Everything" ) );

		QCOMPARE
		(
			fixture.controller.replace_subtree
			(
				JsonPointer::parse ( "/box" ), node_of ( R"({"k":2})" ), QStringLiteral ( "Replace" )
			),
			EditOutcome::Applied
		);

		QCOMPARE ( fixture.controller.set_string  ( JsonPointer::parse ( "/name" ), QStringLiteral ( "b" ) ), EditOutcome::Applied );
		QCOMPARE ( fixture.controller.set_boolean ( JsonPointer::parse ( "/flag" ), true ),                   EditOutcome::Applied );

		QCOMPARE ( fixture.controller.rename_key    ( JsonPointer::parse ( "/name" ), QStringLiteral ( "label" ) ), EditOutcome::Applied );
		QCOMPARE ( fixture.controller.delete_node   ( JsonPointer::parse ( "/items/0" ) ),                          EditOutcome::Applied );
		QCOMPARE ( fixture.controller.move_child    ( JsonPointer::parse ( "/items" ), 0, 1 ),                      EditOutcome::Applied );
		QCOMPARE ( fixture.controller.append_element ( JsonPointer::parse ( "/items" ), JsonNode::make_number ( QStringLiteral ( "40" ) ), QStringLiteral ( "Add" ) ), EditOutcome::Applied );
		QCOMPARE ( fixture.controller.change_type   ( JsonPointer::parse ( "/flag" ), JsonKind::String ),           EditOutcome::Applied );
		QCOMPARE ( fixture.controller.sort_array    ( JsonPointer::parse ( "/items" ), std::nullopt, Qt::AscendingOrder ), EditOutcome::Applied );
	}

	// Nine edits, one step. Any push site that bypasses push_command shows up here as a count above one.

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );

	const QString edited = fixture.text ();

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30],"name":"a","flag":false,"box":{"k":1}})" ) );

	fixture.controller.redo ();

	QCOMPARE ( fixture.text (), edited );
}

void TestUndoController::a_nested_macro_scope_leaves_the_group_to_the_outer_one ()
{
	// A scope opened inside a live one must do NOTHING -- neither open a group nor end one -- so the outer scope
	// still decides where the single undo step begins and ends.
	//
	// It became reachable when EDITOR-18's paste grew the ability to both assign cells and append elements: each half
	// was already a group of its own, and the paste is one gesture over the two. Before this the inner destructor
	// closed the outer's macro early, and every push after it escaped the group -- three undo steps for one paste.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30]})" );

	{
		UndoController::MacroScope outer ( fixture.controller, QStringLiteral ( "Outer" ) );

		fixture.controller.delete_node ( JsonPointer::parse ( "/items/2" ) );

		{
			UndoController::MacroScope inner ( fixture.controller, QStringLiteral ( "Inner" ) );

			fixture.controller.delete_node ( JsonPointer::parse ( "/items/1" ) );
		}

		// The edit AFTER the inner scope closed is the one that escaped: it is what makes this more than a count of
		// two, and it is why the case does not end at the inner brace.

		fixture.controller.set_number ( JsonPointer::parse ( "/items/0" ), QStringLiteral ( "99" ) );
	}

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[99]})" ) );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30]})" ) );

	// And a nested scope that opens BEFORE the outer one has pushed anything is the same no-op, which is the
	// ordering EDITOR-18's paste actually produces when every assignment turns out to be growth.

	fixture.load ( R"({"items":[1,2]})" );

	{
		UndoController::MacroScope outer ( fixture.controller, QStringLiteral ( "Outer" ) );

		{
			UndoController::MacroScope inner ( fixture.controller, QStringLiteral ( "Inner" ) );

			fixture.controller.delete_node ( JsonPointer::parse ( "/items/1" ) );
		}

		fixture.controller.delete_node ( JsonPointer::parse ( "/items/0" ) );
	}

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[]})" ) );
}

void TestUndoController::a_macro_scope_reports_whether_anything_reached_the_stack ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30]})" );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Move Nodes" ) );

		QVERIFY ( !macro.pushed () );

		fixture.controller.move_child ( JsonPointer::parse ( "/items" ), 1, 1 );

		QVERIFY2 ( !macro.pushed (), "a no-op reaches no command onto the stack, so the group is still empty" );

		fixture.controller.move_child ( JsonPointer::parse ( "/items" ), 0, 2 );

		QVERIFY ( macro.pushed () );
	}

	// And the stack is usable again afterwards: a second scope opens cleanly rather than nesting inside the first.

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Delete Nodes" ) );

		fixture.controller.delete_node ( JsonPointer::parse ( "/items/0" ) );
	}

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[30,10]})" ) );

	fixture.controller.undo ();
	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30]})" ) );
}

void TestUndoController::delete_nodes_removes_every_element_it_was_given ()
{
	// THE PHASE'S ONE ORDERING RULE. An element's pointer token IS its position, so removing [1] renames [2] to [1]:
	// a loop walking the selection forward would delete /items/1, then re-read /items/2 -- which is now the element
	// that WAS at 3 -- and leave the intended one standing.
	//
	// The targets are three of five and are all MIDDLE elements, so a forward loop cannot accidentally be right: it
	// removes 20, then 40, then runs off the end, leaving [10, 30, 50] with the wrong two survivors rather than a
	// short list that a size assertion alone would catch.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30,40,50]})" );

	const QList<JsonPointer> targets
	{
		JsonPointer::parse ( "/items/1" ),
		JsonPointer::parse ( "/items/2" ),
		JsonPointer::parse ( "/items/3" )
	};

	QCOMPARE ( fixture.controller.delete_nodes ( targets ), 3 );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,50]})" ) );

	// And the order it is GIVEN them in must not matter, since it sorts for itself -- the caller's set arrives in
	// document order today, and a surface that handed them over click-order would otherwise silently break the rule.

	Fixture reversed;
	reversed.load ( R"({"items":[10,20,30,40,50]})" );

	QCOMPARE ( reversed.controller.delete_nodes ( { targets [ 2 ], targets [ 0 ], targets [ 1 ] } ), 3 );
	QCOMPARE ( reversed.text (), QStringLiteral ( R"({"items":[10,50]})" ) );
}

void TestUndoController::delete_nodes_is_one_undo_step_whatever_it_removed ()
{
	// EDIT-14's central claim, and the one a user checks by pressing Ctrl+Z once.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30,40]})" );

	QCOMPARE ( fixture.controller.delete_nodes
	(
		{
			JsonPointer::parse ( "/items/0" ),
			JsonPointer::parse ( "/items/1" ),
			JsonPointer::parse ( "/items/2" ),
			JsonPointer::parse ( "/items/3" )
		}
	), 4 );

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[]})" ) );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,20,30,40]})" ) );
	QVERIFY2 ( !fixture.controller.can_undo (), "four deletes must undo as ONE step" );
	QVERIFY  ( !fixture.document.is_dirty () );
}

void TestUndoController::delete_nodes_removes_object_members_by_key ()
{
	// The other half of the parent kinds. A member's pointer is its key and survives a sibling's removal either way,
	// which is exactly why the ordering rule is stated once for both rather than made to depend on the kind.

	Fixture fixture;
	fixture.load ( R"({"root":{"a":1,"b":2,"c":3,"d":4}})" );

	QCOMPARE ( fixture.controller.delete_nodes
	(
		{ JsonPointer::parse ( "/root/b" ), JsonPointer::parse ( "/root/c" ) }
	), 2 );

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"root":{"a":1,"d":4}})" ) );
}

void TestUndoController::delete_nodes_skips_what_it_cannot_remove ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[10,20,30]})" );

	// A pointer that names nothing, and the root -- skipped rather than failing the group, so one stale entry in a
	// selection does not cost the user the other three deletions.

	QCOMPARE ( fixture.controller.delete_nodes
	(
		{ JsonPointer::parse ( "/items/1" ), JsonPointer::parse ( "/missing" ), JsonPointer () }
	), 1 );

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[10,30]})" ) );

	// Nothing removable at all leaves NO undo step, which is the lazy macro doing its job: an empty group would
	// otherwise dirty the document and add a step that undoes nothing.

	Fixture nothing;
	nothing.load ( R"({"items":[10,20,30]})" );

	QCOMPARE ( nothing.controller.delete_nodes ( { JsonPointer::parse ( "/missing" ) } ), 0 );

	QVERIFY ( !nothing.controller.can_undo () );
	QVERIFY ( !nothing.document.is_dirty () );
}

void TestUndoController::repeated_pastes_append_in_order_and_insert_in_reverse ()
{
	// The fact MainWindow's multi-node paste derives its iteration order from (EDIT-14). paste_node applies EDIT-03's
	// placement rule, which has two halves that behave OPPOSITELY under repetition: a container APPENDS, so pasting
	// forward keeps the copied order, while a scalar inserts at a FIXED sibling slot, so each arrival pushes the
	// previous one further away and pasting forward would land the run reversed.
	//
	// Pinned here rather than at the call site because it is this method's behaviour, not the window's -- and if it
	// ever changed, the window's loop would silently start producing the wrong order.

	Fixture container;
	container.load ( R"({"items":[]})" );

	container.controller.paste_node ( JsonPointer::parse ( "/items" ), JsonNode::make_number ( QStringLiteral ( "1" ) ) );
	container.controller.paste_node ( JsonPointer::parse ( "/items" ), JsonNode::make_number ( QStringLiteral ( "2" ) ) );

	QCOMPARE ( container.text (), QStringLiteral ( R"({"items":[1,2]})" ) );

	Fixture scalar;
	scalar.load ( R"({"items":[0,9]})" );

	scalar.controller.paste_node ( JsonPointer::parse ( "/items/0" ), JsonNode::make_number ( QStringLiteral ( "1" ) ) );
	scalar.controller.paste_node ( JsonPointer::parse ( "/items/0" ), JsonNode::make_number ( QStringLiteral ( "2" ) ) );

	QCOMPARE ( scalar.text (), QStringLiteral ( R"({"items":[0,2,1,9]})" ) );
}

//=====================================================================================================================
// Change batching (NFR-03).
//
// EVERY OBSERVER OF THIS DOCUMENT RE-DERIVES ITS PROJECTION ONCE PER NOTIFICATION. A grouped gesture applies n
// commands, so without batching an n-cell column paste costs n whole-document re-serializations in CodeView, n
// re-renders in TextView and n tree diffs -- measured at 108 ms for a NINE-row paste in a 4,000-element document,
// where the nine rows were never the variable.
//
// These cases count emissions rather than timing anything, so they state the rule instead of a machine's speed.
//=====================================================================================================================

namespace
{
	// Records every node_changed the document emits, so a case can assert HOW MANY and WHICH SUBTREE.

	struct ChangeRecorder
	{
		QList<QPair<JsonPointer, DocumentChange>> changes;

		explicit ChangeRecorder ( JsonDocument& document )
		{
			QObject::connect
			(
				&document, &JsonDocument::node_changed,
				[ this ] ( const JsonPointer& pointer, DocumentChange change ) { changes.append ( { pointer, change } ); }
			);
		}

		int count () const { return static_cast<int> ( changes.size () ); }
	};
}

void TestUndoController::a_grouped_gesture_notifies_once_naming_the_common_ancestor ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[{"a":1,"b":2},{"a":3,"b":4},{"a":5,"b":6}],"other":"untouched"})" );

	ChangeRecorder recorder ( fixture.document );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Paste Column" ) );

		fixture.controller.set_number ( JsonPointer::parse ( "/items/0/b" ), QStringLiteral ( "20" ) );
		fixture.controller.set_number ( JsonPointer::parse ( "/items/1/b" ), QStringLiteral ( "40" ) );
		fixture.controller.set_number ( JsonPointer::parse ( "/items/2/b" ), QStringLiteral ( "60" ) );
	}

	// ONE notification for three edits -- and it arrives at the CLOSE of the group, not during it, so an observer
	// never sees the document half-edited.

	QCOMPARE ( recorder.count (), 1 );

	// Naming the deepest subtree that contains everything the group touched. Not the root: an observer of /other has
	// nothing to re-derive, and collapsing to the root would tell it otherwise.

	QCOMPARE ( recorder.changes.first ().first,  JsonPointer::parse ( "/items" ) );
	QCOMPARE ( recorder.changes.first ().second, DocumentChange::SubtreeReplaced );
}

void TestUndoController::an_ungrouped_edit_still_notifies_with_its_own_change_kind ()
{
	// The opposite half, so neither case passes against a build that batches everything or nothing. A single edit is
	// unchanged in both respects: it notifies immediately, and it says what KIND of change it was -- which is what
	// lets JsonTableModel patch one cell rather than resync a whole table.

	Fixture fixture;
	fixture.load ( R"({"items":[{"a":1,"b":2}]})" );

	ChangeRecorder recorder ( fixture.document );

	fixture.controller.set_number ( JsonPointer::parse ( "/items/0/b" ), QStringLiteral ( "20" ) );

	QCOMPARE ( recorder.count (), 1 );

	QCOMPARE ( recorder.changes.first ().first,  JsonPointer::parse ( "/items/0/b" ) );
	QCOMPARE ( recorder.changes.first ().second, DocumentChange::ValueChanged );
}

void TestUndoController::a_group_that_applied_nothing_notifies_nothing ()
{
	// D19's shape at the notification layer: a group whose every edit turned out to be Unchanged or Rejected has not
	// changed the document, so announcing a SubtreeReplaced would send every observer to re-derive an identical
	// projection -- and would say something false about the document besides.

	Fixture fixture;
	fixture.load ( R"({"items":[10,20],"name":"a"})" );

	ChangeRecorder recorder ( fixture.document );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Nothing" ) );

		QCOMPARE ( fixture.controller.set_string ( JsonPointer::parse ( "/name" ),  QStringLiteral ( "a" ) ), EditOutcome::Unchanged );
		QCOMPARE ( fixture.controller.set_number ( JsonPointer::parse ( "/items/0" ), QStringLiteral ( "10" ) ), EditOutcome::Unchanged );
	}

	QCOMPARE ( recorder.count (), 0 );
}

void TestUndoController::undoing_and_redoing_a_group_notifies_once_each ()
{
	// A macro's children are re-applied ONE AT A TIME on the way back, each notifying -- so an unbatched undo costs
	// exactly what the unbatched gesture did, and the fix would have covered only half the round trip.

	Fixture fixture;
	fixture.load ( R"({"items":[{"a":1,"b":2},{"a":3,"b":4},{"a":5,"b":6}]})" );

	{
		UndoController::MacroScope macro ( fixture.controller, QStringLiteral ( "Paste Column" ) );

		fixture.controller.set_number ( JsonPointer::parse ( "/items/0/b" ), QStringLiteral ( "20" ) );
		fixture.controller.set_number ( JsonPointer::parse ( "/items/1/b" ), QStringLiteral ( "40" ) );
		fixture.controller.set_number ( JsonPointer::parse ( "/items/2/b" ), QStringLiteral ( "60" ) );
	}

	ChangeRecorder recorder ( fixture.document );

	fixture.controller.undo ();

	QCOMPARE ( recorder.count (), 1 );

	fixture.controller.redo ();

	QCOMPARE ( recorder.count (), 2 );

	// And the round trip is still correct, which no count can say.

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[{"a":1,"b":20},{"a":3,"b":40},{"a":5,"b":60}]})" ) );
}

QTEST_GUILESS_MAIN ( TestUndoController )

void TestUndoController::the_duplicate_key_policy_governs_both_guards ()
{
	Fixture fixture;

	// SET-03a. VAL-02 refuses an edit that would give an object a second member with a key it already carries, and
	// this is the switch that defers to a user who wants the duplicates RFC 8259 permits.
	//
	// WRITTEN AS AN OPPOSING PAIR, and over BOTH guards. One of them alone would pass against a build that wired the
	// flag into insert_at and forgot rename_key -- which is the shape this codebase keeps finding (a rule implemented
	// on one route and not its twin), and the reason both now ask through Validator rather than spelling has_member
	// out separately.

	fixture.load ( R"({"target":{"a":1,"b":2}})" );

	const JsonPointer target = JsonPointer::parse ( QStringLiteral ( "/target" ) );

	QVERIFY ( !fixture.controller.allow_duplicate_keys () );                // The default is the strict answer.

	// Refused by default: an add that collides, and a rename onto a sibling's key.

	QCOMPARE ( fixture.controller.add_node ( target, JsonKind::Null, QStringLiteral ( "a" ) ), EditOutcome::Rejected );

	QCOMPARE
	(
		fixture.controller.rename_key ( JsonPointer::parse ( QStringLiteral ( "/target/a" ) ), QStringLiteral ( "b" ) ),
		EditOutcome::Rejected
	);

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"target":{"a":1,"b":2}})" ) );

	// And allowed once the user says so -- the same two edits, the same document.

	fixture.controller.set_allow_duplicate_keys ( true );

	QCOMPARE ( fixture.controller.add_node ( target, JsonKind::Null, QStringLiteral ( "a" ) ), EditOutcome::Applied );

	QCOMPARE
	(
		fixture.controller.rename_key ( JsonPointer::parse ( QStringLiteral ( "/target/b" ) ), QStringLiteral ( "a" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"target":{"a":1,"a":2,"a":null}})" ) );
}

void TestUndoController::the_duplicate_key_policy_never_governs_naming ()
{
	Fixture fixture;

	// THE BOUNDARY, and it is what keeps the setting from meaning more than it says. EDIT-07's Duplicate and EDIT-16's
	// column naming avoid a collision by choosing a DIFFERENT key -- a rule about what to call a thing rather than
	// about whether to allow it -- so neither consults this flag.
	//
	// Without this, "allow duplicates" would quietly turn Duplicate into a command that produces two members with one
	// key, which nobody asked for and which the (copy) sequence exists precisely to avoid.

	fixture.load ( R"({"target":{"a":1}})" );

	fixture.controller.set_allow_duplicate_keys ( true );

	QCOMPARE ( fixture.controller.duplicate_node ( JsonPointer::parse ( QStringLiteral ( "/target/a" ) ) ), EditOutcome::Applied );

	// An escaped literal rather than a raw string: EDIT-07's suffix ends in `)`, and `)"` closes an R"( ... )" early,
	// so the raw form terminates in the middle of the expected text and the file stops compiling several lines later.

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":{\"a\":1,\"a (copy)\":1}}" ) );
}

#include "tst_undo_controller.moc"

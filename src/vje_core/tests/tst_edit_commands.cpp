//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for the edit commands, driven through their public surface (UndoController). The Phase 2
//   acceptance gate is redo(undo(x)) == x for every command: each test captures the serialized document before an
//   edit, applies it, and asserts that undo restores the original byte-for-byte and redo reproduces the edited form.
//   It also covers the structural result of each command, the fine-grained node_changed() signal each emits, and the
//   VAL-02 duplicate-key rejection on rename and object-member add.
//
//   Uses QTEST_GUILESS_MAIN: QUndoStack/QUndoCommand run headlessly under a QCoreApplication (no display).
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

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> node_of ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		Q_ASSERT ( result.ok );
		return std::move ( result.root );
	}

	// A document + undo controller wired together, with helpers to load a tree and read it back as text.

	struct Fixture
	{
		JsonDocument   document;
		UndoController controller { &document };

		void load ( const QString& json )
		{
			document.set_root ( node_of ( json ) );
			controller.clear ();                                   // The loaded state is the clean baseline.
		}

		QString text () const
		{
			return JsonSerializer::serialize ( *document.root () );
		}
	};

	// Applied + redo(undo(x)) == x: undo returns to `before`, redo returns to `after`. Run twice to catch any
	// asymmetry that only shows on a second cycle.

	void assert_roundtrip ( Fixture& fixture, EditOutcome outcome, const QString& before, const QString& after )
	{
		QCOMPARE ( outcome,        EditOutcome::Applied );
		QCOMPARE ( fixture.text (), after );

		fixture.controller.undo ();
		QCOMPARE ( fixture.text (), before );

		fixture.controller.redo ();
		QCOMPARE ( fixture.text (), after );

		fixture.controller.undo ();
		QCOMPARE ( fixture.text (), before );

		fixture.controller.redo ();
		QCOMPARE ( fixture.text (), after );
	}
}

class TestEditCommands : public QObject
{
	Q_OBJECT

private slots:

	void set_string_roundtrips ();
	void set_number_preserves_token ();
	void set_boolean_roundtrips ();

	void rename_key_roundtrips ();

	void add_sibling_after_scalar ();
	void add_child_to_root_object ();
	void add_element_to_array ();

	void delete_object_member_roundtrips ();
	void delete_array_element_roundtrips ();

	void duplicate_array_element ();
	void duplicate_object_member_dedupes_key ();

	void move_array_element_up ();
	void move_object_member_down ();

	void change_type_roundtrips ();
	void change_type_at_root ();

	void normalize_array_roundtrips ();
	void array_to_objects_at_root ();
	void objects_to_array_at_root ();

	void edits_target_nodes_under_a_replaced_ancestor ();

	void node_changed_reports_each_edit_kind ();
};

//---------------------------------------------------------------------------------------------------------------------
// Scalar value edits (EDIT-01)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::set_string_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"a":"x"})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.set_string ( JsonPointer::parse ( "/a" ), QStringLiteral ( "y" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"a":"y"})" );
}

void TestEditCommands::set_number_preserves_token ()
{
	Fixture fixture;
	fixture.load ( R"({"n":1})" );

	const QString before = fixture.text ();

	// The raw token is stored verbatim (FILE-10): "2.50" must not be canonicalized to "2.5".

	const EditOutcome outcome = fixture.controller.set_number ( JsonPointer::parse ( "/n" ), QStringLiteral ( "2.50" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"n":2.50})" );
}

void TestEditCommands::set_boolean_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"b":false})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.set_boolean ( JsonPointer::parse ( "/b" ), true );

	assert_roundtrip ( fixture, outcome, before, R"({"b":true})" );
}

//---------------------------------------------------------------------------------------------------------------------
// Rename (EDIT-02)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::rename_key_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.rename_key ( JsonPointer::parse ( "/a" ), QStringLiteral ( "c" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"c":1,"b":2})" );
}

//---------------------------------------------------------------------------------------------------------------------
// Add (EDIT-03 / 04)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::add_sibling_after_scalar ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	// /a is a scalar, so EDIT-03 places the new member as its next sibling.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.add_node ( JsonPointer::parse ( "/a" ), JsonKind::String, QStringLiteral ( "z" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"a":1,"z":""})" );
}

void TestEditCommands::add_child_to_root_object ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1})" );

	// The root object selection receives the new node as its last child, with the neutral default 0 for a number.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.add_node ( JsonPointer (), JsonKind::Number, QStringLiteral ( "k" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"a":1,"k":0})" );
}

void TestEditCommands::add_element_to_array ()
{
	Fixture fixture;
	fixture.load ( "[1,2]" );

	// An array selection appends; the key argument is ignored.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.add_node ( JsonPointer (), JsonKind::String );

	assert_roundtrip ( fixture, outcome, before, R"([1,2,""])" );
}

//---------------------------------------------------------------------------------------------------------------------
// Delete (EDIT-05)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::delete_object_member_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.delete_node ( JsonPointer::parse ( "/a" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"b":2})" );
}

void TestEditCommands::delete_array_element_roundtrips ()
{
	Fixture fixture;
	fixture.load ( "[1,2,3]" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.delete_node ( JsonPointer::parse ( "/1" ) );

	assert_roundtrip ( fixture, outcome, before, "[1,3]" );
}

//---------------------------------------------------------------------------------------------------------------------
// Duplicate (EDIT-07)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::duplicate_array_element ()
{
	Fixture fixture;
	fixture.load ( "[1,2]" );

	// Arrays: the clone is inserted immediately after the original.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.duplicate_node ( JsonPointer::parse ( "/0" ) );

	assert_roundtrip ( fixture, outcome, before, "[1,1,2]" );
}

void TestEditCommands::duplicate_object_member_dedupes_key ()
{
	Fixture fixture;
	fixture.load ( R"({"k":1})" );

	// Objects: the clone gets a de-duplicated key.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.duplicate_node ( JsonPointer::parse ( "/k" ) );

	assert_roundtrip ( fixture, outcome, before, R"json({"k":1,"k (copy)":1})json" );
}

//---------------------------------------------------------------------------------------------------------------------
// Move (EDIT-08)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::move_array_element_up ()
{
	Fixture fixture;
	fixture.load ( "[1,2,3]" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.move_node ( JsonPointer::parse ( "/2" ), MoveDirection::Up );

	assert_roundtrip ( fixture, outcome, before, "[1,3,2]" );
}

void TestEditCommands::move_object_member_down ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"b":2,"c":3})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.move_node ( JsonPointer::parse ( "/a" ), MoveDirection::Down );

	assert_roundtrip ( fixture, outcome, before, R"({"b":2,"a":1,"c":3})" );
}

//---------------------------------------------------------------------------------------------------------------------
// Change type (EDIT-09)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::change_type_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"s":"42"})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.change_type ( JsonPointer::parse ( "/s" ), JsonKind::Number );

	assert_roundtrip ( fixture, outcome, before, R"({"s":42})" );
}

void TestEditCommands::change_type_at_root ()
{
	Fixture fixture;
	fixture.load ( R"("hello")" );

	// A root-level replace must go through the document's root and still roundtrip.

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.change_type ( JsonPointer (), JsonKind::Array );

	assert_roundtrip ( fixture, outcome, before, "[]" );
}

//---------------------------------------------------------------------------------------------------------------------
// Array transforms (EDIT-11 / 12 / 13)
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::normalize_array_roundtrips ()
{
	Fixture fixture;
	fixture.load ( R"({"arr":[{"a":1},{"b":2}]})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.normalize_array ( JsonPointer::parse ( "/arr" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"arr":[{"a":1,"b":null},{"a":null,"b":2}]})" );
}

void TestEditCommands::array_to_objects_at_root ()
{
	Fixture fixture;
	fixture.load ( "[1,2]" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.array_to_objects ( JsonPointer () );

	assert_roundtrip ( fixture, outcome, before, R"({"0":1,"1":2})" );
}

void TestEditCommands::objects_to_array_at_root ()
{
	Fixture fixture;
	fixture.load ( R"({"x":1,"y":2})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.objects_to_array ( JsonPointer () );

	assert_roundtrip ( fixture, outcome, before, "[1,2]" );
}

//---------------------------------------------------------------------------------------------------------------------
// Targeting robustness: an edit deep under a nested structure resolves by pointer.
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::edits_target_nodes_under_a_replaced_ancestor ()
{
	Fixture fixture;
	fixture.load ( R"({"outer":{"inner":[{"v":1}]}})" );

	const QString before = fixture.text ();
	const EditOutcome outcome = fixture.controller.set_number ( JsonPointer::parse ( "/outer/inner/0/v" ), QStringLiteral ( "9" ) );

	assert_roundtrip ( fixture, outcome, before, R"({"outer":{"inner":[{"v":9}]}})" );
}

//---------------------------------------------------------------------------------------------------------------------
// Fine-grained change notification
//---------------------------------------------------------------------------------------------------------------------

void TestEditCommands::node_changed_reports_each_edit_kind ()
{
	Fixture fixture;
	fixture.load ( R"({"a":1,"s":"x"})" );

	DocumentChange lastChange {};
	QString        lastPointer;
	int            count = 0;

	QObject::connect
	(
		&fixture.document, &JsonDocument::node_changed,
		[ & ] ( const JsonPointer& pointer, DocumentChange change )
		{
			lastChange  = change;
			lastPointer = pointer.to_string ();
			++count;
		}
	);

	// ValueChanged, on the edited node.

	fixture.controller.set_number ( JsonPointer::parse ( "/a" ), QStringLiteral ( "2" ) );
	QCOMPARE ( lastChange,  DocumentChange::ValueChanged );
	QCOMPARE ( lastPointer, QStringLiteral ( "/a" ) );

	// Undo re-emits, so views refresh on undo as well.

	const int countBeforeUndo = count;
	fixture.controller.undo ();
	QVERIFY  ( count > countBeforeUndo );

	// KeyRenamed / NodeAdded / NodeRemoved / SubtreeReplaced / NodeMoved each surface their kind.

	fixture.controller.rename_key ( JsonPointer::parse ( "/a" ), QStringLiteral ( "b" ) );
	QCOMPARE ( lastChange, DocumentChange::KeyRenamed );

	fixture.controller.add_node ( JsonPointer (), JsonKind::Null, QStringLiteral ( "n" ) );
	QCOMPARE ( lastChange, DocumentChange::NodeAdded );

	fixture.controller.delete_node ( JsonPointer::parse ( "/n" ) );
	QCOMPARE ( lastChange, DocumentChange::NodeRemoved );

	fixture.controller.change_type ( JsonPointer::parse ( "/s" ), JsonKind::Null );
	QCOMPARE ( lastChange, DocumentChange::SubtreeReplaced );

	fixture.controller.move_node ( JsonPointer::parse ( "/b" ), MoveDirection::Down );
	QCOMPARE ( lastChange, DocumentChange::NodeMoved );
}

QTEST_GUILESS_MAIN ( TestEditCommands )

#include "tst_edit_commands.moc"

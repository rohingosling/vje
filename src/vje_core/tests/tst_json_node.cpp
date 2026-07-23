//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonNode: construction of each kind, array/object access, duplicate-tolerant object
//   members, deep clone, and order-sensitive structural equality (FILE-04).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestJsonNode : public QObject
{
	Q_OBJECT

private slots:

	void scalars_carry_their_kind_and_value ();
	void array_append_and_access ();
	void object_append_preserves_order ();
	void object_tolerates_duplicate_keys ();
	void parent_back_pointers_are_set ();
	void clone_is_a_deep_independent_copy ();
	void equals_is_order_sensitive_for_objects ();
	void equals_distinguishes_number_tokens ();
};

void TestJsonNode::scalars_carry_their_kind_and_value ()
{
	QCOMPARE ( JsonNode::make_null ()->kind (),          JsonKind::Null );
	QCOMPARE ( JsonNode::make_boolean ( true )->kind (), JsonKind::Boolean );
	QCOMPARE ( JsonNode::make_number ( "1.0" )->kind (), JsonKind::Number );
	QCOMPARE ( JsonNode::make_string ( "hi" )->kind (),  JsonKind::String );

	QCOMPARE ( JsonNode::make_boolean ( true )->boolean_value (), true );
	QCOMPARE ( JsonNode::make_number ( "1e3" )->number_token (),  QStringLiteral ( "1e3" ) );
	QCOMPARE ( JsonNode::make_string ( "hi" )->string_value (),   QStringLiteral ( "hi" ) );

	QVERIFY ( JsonNode::make_null ()->is_scalar () );
	QVERIFY ( JsonNode::make_array ()->is_container () );
}

void TestJsonNode::array_append_and_access ()
{
	auto array = JsonNode::make_array ();

	array->append_element ( JsonNode::make_number ( "1" ) );
	array->append_element ( JsonNode::make_number ( "2" ) );

	QCOMPARE ( array->array_size (), 2 );
	QCOMPARE ( array->array_element ( 0 )->number_token (), QStringLiteral ( "1" ) );
	QCOMPARE ( array->array_element ( 1 )->number_token (), QStringLiteral ( "2" ) );
	QVERIFY  ( array->array_element ( 2 ) == nullptr );
	QVERIFY  ( array->array_element ( -1 ) == nullptr );
}

void TestJsonNode::object_append_preserves_order ()
{
	auto object = JsonNode::make_object ();

	object->append_member ( "z", JsonNode::make_number ( "1" ) );
	object->append_member ( "a", JsonNode::make_number ( "2" ) );

	QCOMPARE ( object->member_count (), 2 );
	QCOMPARE ( object->member_key ( 0 ), QStringLiteral ( "z" ) );
	QCOMPARE ( object->member_key ( 1 ), QStringLiteral ( "a" ) );

	QVERIFY  ( object->has_member ( "a" ) );
	QVERIFY  ( !object->has_member ( "q" ) );
	QCOMPARE ( object->find_member ( "z" )->number_token (), QStringLiteral ( "1" ) );
	QVERIFY  ( object->find_member ( "q" ) == nullptr );
}

void TestJsonNode::object_tolerates_duplicate_keys ()
{
	auto object = JsonNode::make_object ();

	object->append_member ( "a", JsonNode::make_number ( "1" ) );
	object->append_member ( "a", JsonNode::make_number ( "2" ) );

	QCOMPARE ( object->member_count (), 2 );
	QCOMPARE ( object->key_count ( "a" ), 2 );

	// find_member returns the FIRST occurrence.

	QCOMPARE ( object->find_member ( "a" )->number_token (), QStringLiteral ( "1" ) );
}

void TestJsonNode::parent_back_pointers_are_set ()
{
	auto object = JsonNode::make_object ();
	JsonNode* child = object->append_member ( "a", JsonNode::make_array () );
	JsonNode* grandchild = child->append_element ( JsonNode::make_null () );

	QCOMPARE ( child->parent (), object.get () );
	QCOMPARE ( grandchild->parent (), child );
	QVERIFY  ( object->parent () == nullptr );
}

void TestJsonNode::clone_is_a_deep_independent_copy ()
{
	auto object = JsonNode::make_object ();
	object->append_member ( "n", JsonNode::make_number ( "42" ) );

	auto copy = object->clone ();

	QVERIFY ( object->equals ( *copy ) );

	// Mutating the original must not touch the clone.

	object->find_member ( "n" )->set_number_token ( "99" );

	QCOMPARE ( copy->find_member ( "n" )->number_token (), QStringLiteral ( "42" ) );
	QVERIFY  ( !object->equals ( *copy ) );

	// Clone parent links are internal to the copy.

	QCOMPARE ( copy->member_value ( 0 )->parent (), copy.get () );
}

void TestJsonNode::equals_is_order_sensitive_for_objects ()
{
	auto first = JsonNode::make_object ();
	first->append_member ( "a", JsonNode::make_number ( "1" ) );
	first->append_member ( "b", JsonNode::make_number ( "2" ) );

	auto second = JsonNode::make_object ();
	second->append_member ( "b", JsonNode::make_number ( "2" ) );
	second->append_member ( "a", JsonNode::make_number ( "1" ) );

	// Same members, different order => not equal (member order is significant, FILE-04).

	QVERIFY ( !first->equals ( *second ) );
}

void TestJsonNode::equals_distinguishes_number_tokens ()
{
	// "1" and "1.0" are numerically equal but textually distinct (FILE-10).

	QVERIFY ( !JsonNode::make_number ( "1" )->equals ( *JsonNode::make_number ( "1.0" ) ) );
	QVERIFY (  JsonNode::make_number ( "1.0" )->equals ( *JsonNode::make_number ( "1.0" ) ) );
}

QTEST_APPLESS_MAIN ( TestJsonNode )

#include "tst_json_node.moc"

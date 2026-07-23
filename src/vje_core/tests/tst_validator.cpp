//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for Validator (VAL-01..03): RFC 8259 validation with a reported position; the rejectDuplicates flag
//   distinguishing the Code View commit / edit paths (fail on a duplicate key) from load (accept, list); the
//   introduces_duplicate edit guard with an ignore-self slot for rename; and JSON-number validation for Form input.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/Validator.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestValidator : public QObject
{
	Q_OBJECT

private slots:

	void valid_text_passes ();
	void malformed_text_reports_position ();
	void duplicates_accepted_when_not_rejecting ();
	void duplicates_rejected_on_edit_path ();
	void introduces_duplicate_guard ();
	void introduces_duplicate_ignores_self ();
	void number_validation_data ();
	void number_validation ();
};

void TestValidator::valid_text_passes ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "{\"a\":1}" ), true );

	QVERIFY ( result.ok );
	QVERIFY ( result.root != nullptr );
}

void TestValidator::malformed_text_reports_position ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "[1,,2]" ), true );

	QVERIFY ( !result.ok );
	QVERIFY ( result.root == nullptr );
	QVERIFY ( !result.issue.message.isEmpty () );
	QCOMPARE ( result.issue.line, 1 );
	QVERIFY ( result.issue.column > 0 );
}

void TestValidator::duplicates_accepted_when_not_rejecting ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "{\"a\":1,\"a\":2}" ), false );

	QVERIFY ( result.ok );                                 // Load path: duplicates accepted and listed.
	QCOMPARE ( static_cast<int> ( result.duplicateKeys.size () ), 1 );
}

void TestValidator::duplicates_rejected_on_edit_path ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "{\"a\":1,\"a\":2}" ), true );

	QVERIFY ( !result.ok );                                // Code View commit path: a duplicate is a rejection.
	QVERIFY ( result.root == nullptr );
	QVERIFY ( result.issue.message.contains ( QStringLiteral ( "Duplicate" ) ) );
	QVERIFY ( result.issue.line > 0 );
}

void TestValidator::introduces_duplicate_guard ()
{
	std::unique_ptr<JsonNode> object = JsonNode::make_object ();
	object->append_member ( QStringLiteral ( "a" ), JsonNode::make_null () );
	object->append_member ( QStringLiteral ( "b" ), JsonNode::make_null () );

	QVERIFY ( Validator::introduces_duplicate ( *object, QStringLiteral ( "a" ) ) );
	QVERIFY ( !Validator::introduces_duplicate ( *object, QStringLiteral ( "c" ) ) );
}

void TestValidator::introduces_duplicate_ignores_self ()
{
	std::unique_ptr<JsonNode> object = JsonNode::make_object ();
	object->append_member ( QStringLiteral ( "a" ), JsonNode::make_null () );
	object->append_member ( QStringLiteral ( "b" ), JsonNode::make_null () );

	// Renaming member 0 to "a" (its own key) is not a collision; renaming it to "b" is.

	QVERIFY ( !Validator::introduces_duplicate ( *object, QStringLiteral ( "a" ), 0 ) );
	QVERIFY ( Validator::introduces_duplicate ( *object, QStringLiteral ( "b" ), 0 ) );
}

void TestValidator::number_validation_data ()
{
	QTest::addColumn<QString> ( "text" );
	QTest::addColumn<bool> ( "valid" );

	QTest::newRow ( "int" )        << QStringLiteral ( "42" )     << true;
	QTest::newRow ( "negative" )   << QStringLiteral ( "-1.5" )   << true;
	QTest::newRow ( "exponent" )   << QStringLiteral ( "1e3" )    << true;
	QTest::newRow ( "trimmed" )    << QStringLiteral ( "  7  " )  << true;
	QTest::newRow ( "empty" )      << QStringLiteral ( "" )       << false;
	QTest::newRow ( "word" )       << QStringLiteral ( "abc" )    << false;
	QTest::newRow ( "trailing" )   << QStringLiteral ( "1x" )     << false;
	QTest::newRow ( "hex" )        << QStringLiteral ( "0x1F" )   << false;
	QTest::newRow ( "leading_dot" )<< QStringLiteral ( ".5" )     << false;
	QTest::newRow ( "plus" )       << QStringLiteral ( "+1" )     << false;
}

void TestValidator::number_validation ()
{
	QFETCH ( QString, text );
	QFETCH ( bool, valid );

	QCOMPARE ( Validator::is_valid_number ( text ), valid );
}

QTEST_APPLESS_MAIN ( TestValidator )

#include "tst_validator.moc"

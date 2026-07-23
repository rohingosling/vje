//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for SearchService (FIND-01..04): whole-document matching against member keys and the textual form of
//   scalars (string / number / true / false / null), case sensitivity, document-order results, one-entry-per-node,
//   the empty-query and no-match guards, and Go To pointer resolution.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/SearchService.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> parse ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		return std::move ( result.root );
	}

	QStringList pointer_strings ( const std::vector<JsonPointer>& pointers )
	{
		QStringList out;
		for ( const JsonPointer& pointer : pointers ) { out << pointer.to_string (); }
		return out;
	}

	std::unique_ptr<JsonNode> sample ()
	{
		return parse ( QStringLiteral (
			"{\"name\":\"Alice\",\"age\":30,\"active\":true,\"notes\":null,"
			"\"tags\":[\"admin\",\"name\"],\"child\":{\"name\":\"Bob\"}}" ) );
	}

	std::vector<JsonPointer> find ( const JsonNode& root, const QString& text, bool matchCase = false )
	{
		SearchQuery query;
		query.text      = text;
		query.matchCase = matchCase;
		return SearchService::find_all ( root, query );
	}
}

class TestSearchService : public QObject
{
	Q_OBJECT

private slots:

	void matches_keys_and_values_in_document_order ();
	void matches_scalar_textual_forms ();
	void case_insensitive_by_default ();
	void match_case_option ();
	void one_entry_per_node ();
	void empty_query_matches_nothing ();
	void no_match_is_empty ();
	void go_to_resolves_pointer ();
	void go_to_unresolvable_is_null ();
};

void TestSearchService::matches_keys_and_values_in_document_order ()
{
	std::unique_ptr<JsonNode> root = sample ();

	// "name": the /name key, the "name" string element in tags, and the /child/name key -- pre-order.

	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "name" ) ) ),
	           ( QStringList { QStringLiteral ( "/name" ), QStringLiteral ( "/tags/1" ), QStringLiteral ( "/child/name" ) } ) );
}

void TestSearchService::matches_scalar_textual_forms ()
{
	std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "30" ) ) ),     ( QStringList { QStringLiteral ( "/age" ) } ) );
	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "true" ) ) ),   ( QStringList { QStringLiteral ( "/active" ) } ) );
	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "null" ) ) ),   ( QStringList { QStringLiteral ( "/notes" ) } ) );
	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "Bob" ) ) ),    ( QStringList { QStringLiteral ( "/child/name" ) } ) );
}

void TestSearchService::case_insensitive_by_default ()
{
	std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "ALICE" ) ) ), ( QStringList { QStringLiteral ( "/name" ) } ) );
}

void TestSearchService::match_case_option ()
{
	std::unique_ptr<JsonNode> root = sample ();

	QVERIFY ( find ( *root, QStringLiteral ( "ALICE" ), true ).empty () );
	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "Alice" ), true ) ), ( QStringList { QStringLiteral ( "/name" ) } ) );
}

void TestSearchService::one_entry_per_node ()
{
	// A member whose key AND value both match contributes exactly one entry.

	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"dup\":\"dup\"}" ) );

	QCOMPARE ( pointer_strings ( find ( *root, QStringLiteral ( "dup" ) ) ), ( QStringList { QStringLiteral ( "/dup" ) } ) );
}

void TestSearchService::empty_query_matches_nothing ()
{
	std::unique_ptr<JsonNode> root = sample ();

	QVERIFY ( find ( *root, QString () ).empty () );
}

void TestSearchService::no_match_is_empty ()
{
	std::unique_ptr<JsonNode> root = sample ();

	QVERIFY ( find ( *root, QStringLiteral ( "zzz-not-present" ) ).empty () );
}

void TestSearchService::go_to_resolves_pointer ()
{
	std::unique_ptr<JsonNode> root = sample ();

	bool        ok      = false;
	JsonPointer pointer = JsonPointer::parse ( QStringLiteral ( "/child/name" ), &ok );
	QVERIFY ( ok );

	JsonNode* target = SearchService::go_to ( root.get (), pointer );

	QVERIFY ( target != nullptr );
	QCOMPARE ( target->kind (), JsonKind::String );
	QCOMPARE ( target->string_value (), QStringLiteral ( "Bob" ) );
}

void TestSearchService::go_to_unresolvable_is_null ()
{
	std::unique_ptr<JsonNode> root = sample ();

	bool        ok      = false;
	JsonPointer pointer = JsonPointer::parse ( QStringLiteral ( "/child/missing" ), &ok );
	QVERIFY ( ok );

	QVERIFY ( SearchService::go_to ( root.get (), pointer ) == nullptr );
}

QTEST_APPLESS_MAIN ( TestSearchService )

#include "tst_search_service.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonLexer: token classification, exact source spans, 1-based line/column tracking, and
//   the total handling of malformed input (Invalid tokens rather than exceptions).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonLexer.hpp>

#include <QtTest/QtTest>

#include <vector>

using namespace vje;

class TestJsonLexer : public QObject
{
	Q_OBJECT

private:

	static std::vector<JsonToken> tokenize ( const QString& text );

private slots:

	void classifies_structural_and_scalar_tokens ();
	void text_of_returns_exact_spans ();
	void tracks_line_and_column ();
	void number_grammar ();
	void invalid_number_tails ();
	void invalid_strings ();
	void invalid_literals ();
};

std::vector<JsonToken> TestJsonLexer::tokenize ( const QString& text )
{
	JsonLexer lexer ( text );
	std::vector<JsonToken> tokens;

	for ( ; ; )
	{
		JsonToken token = lexer.next ();
		tokens.push_back ( token );

		if ( ( token.kind == JsonTokenKind::EndOfInput ) || ( token.kind == JsonTokenKind::Invalid ) )
		{
			break;
		}
	}

	return tokens;
}

void TestJsonLexer::classifies_structural_and_scalar_tokens ()
{
	std::vector<JsonToken> tokens = tokenize ( "{ \"a\" : [ true, false, null, 1 ] }" );

	std::vector<JsonTokenKind> kinds;
	for ( const JsonToken& token : tokens )
	{
		kinds.push_back ( token.kind );
	}

	const std::vector<JsonTokenKind> expected =
	{
		JsonTokenKind::BeginObject,
		JsonTokenKind::String,
		JsonTokenKind::NameSeparator,
		JsonTokenKind::BeginArray,
		JsonTokenKind::True,
		JsonTokenKind::ValueSeparator,
		JsonTokenKind::False,
		JsonTokenKind::ValueSeparator,
		JsonTokenKind::Null,
		JsonTokenKind::ValueSeparator,
		JsonTokenKind::Number,
		JsonTokenKind::EndArray,
		JsonTokenKind::EndObject,
		JsonTokenKind::EndOfInput,
	};

	QVERIFY ( kinds == expected );
}

void TestJsonLexer::text_of_returns_exact_spans ()
{
	const QString source = "  \"hello\"  ";
	JsonLexer lexer ( source );

	JsonToken token = lexer.next ();

	QCOMPARE ( token.kind, JsonTokenKind::String );
	QCOMPARE ( lexer.text_of ( token ), QStringLiteral ( "\"hello\"" ) );
}

void TestJsonLexer::tracks_line_and_column ()
{
	// Token positions are 1-based; the number sits on line 2, column 8.

	const QString source = "{\n  \"a\": 12\n}";
	std::vector<JsonToken> tokens = tokenize ( source );

	// Find the Number token.

	JsonToken numberToken;
	for ( const JsonToken& token : tokens )
	{
		if ( token.kind == JsonTokenKind::Number )
		{
			numberToken = token;
		}
	}

	QCOMPARE ( numberToken.line, 2 );
	QCOMPARE ( numberToken.column, 8 );
}

void TestJsonLexer::number_grammar ()
{
	for ( const QString& number : { QStringLiteral ( "0" ),
	                                QStringLiteral ( "-0" ),
	                                QStringLiteral ( "123" ),
	                                QStringLiteral ( "-1.5" ),
	                                QStringLiteral ( "1e10" ),
	                                QStringLiteral ( "-2.5E-3" ),
	                                QStringLiteral ( "123456789012345678901234567890" ) } )
	{
		JsonLexer lexer ( number );
		JsonToken token = lexer.next ();

		QCOMPARE ( token.kind, JsonTokenKind::Number );
		QCOMPARE ( lexer.text_of ( token ), number );
	}
}

void TestJsonLexer::invalid_number_tails ()
{
	// A bare minus, a trailing dot, and a dangling exponent are not valid numbers.

	for ( const QString& text : { QStringLiteral ( "-" ), QStringLiteral ( "1." ), QStringLiteral ( "1e" ) } )
	{
		JsonLexer lexer ( text );
		QCOMPARE ( lexer.next ().kind, JsonTokenKind::Invalid );
	}
}

void TestJsonLexer::invalid_strings ()
{
	// Unterminated, a bad escape, and a raw control character are all Invalid.

	for ( const QString& text : { QStringLiteral ( "\"abc" ),
	                              QStringLiteral ( "\"a\\x\"" ),
	                              QStringLiteral ( "\"a\\u12g4\"" ) } )
	{
		JsonLexer lexer ( text );
		QCOMPARE ( lexer.next ().kind, JsonTokenKind::Invalid );
	}
}

void TestJsonLexer::invalid_literals ()
{
	JsonLexer lexer ( "tru" );
	QCOMPARE ( lexer.next ().kind, JsonTokenKind::Invalid );
}

QTEST_APPLESS_MAIN ( TestJsonLexer )

#include "tst_json_lexer.moc"

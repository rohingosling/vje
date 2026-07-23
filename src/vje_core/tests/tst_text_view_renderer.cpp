//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Golden coverage for TextViewRenderer (EDITOR-06 / SET-06). The eight array table styles and the Markdown list /
//   table object listings are compared byte-for-byte against the spec's section 2.10 examples (the same four-element
//   array of objects), plus the plain key-value listing (aligned / unaligned), the include filters, and a scalar
//   (single-column) array.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/TextViewRenderer.hpp>
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

	// The spec's four-element array of objects (section 2.10 table-style examples).

	std::unique_ptr<JsonNode> sample_array ()
	{
		return parse ( QStringLiteral (
			"[{\"id\":0,\"name\":\"Zero\",\"description\":\"This is the first row.\",\"tag\":0,\"enabled\":true},"
			" {\"id\":1,\"name\":\"One\",\"description\":\"This is the second row.\",\"tag\":0,\"enabled\":true},"
			" {\"id\":2,\"name\":\"Two\",\"description\":\"This is the third row.\",\"tag\":1,\"enabled\":true},"
			" {\"id\":3,\"name\":\"Three\",\"description\":\"This is the fourth row.\",\"tag\":0,\"enabled\":false}]" ) );
	}

	std::unique_ptr<JsonNode> sample_object ()
	{
		return parse ( QStringLiteral (
			"{\"id\":0,\"name\":\"Zero\",\"description\":\"This is the first row.\",\"tag\":0,\"enabled\":true}" ) );
	}
}

class TestTextViewRenderer : public QObject
{
	Q_OBJECT

private slots:

	void academic ();
	void compact ();
	void columnar ();
	void spreadsheet ();
	void minimal ();
	void markdown_table ();
	void csv ();
	void tsv ();

	void object_plain_unaligned ();
	void object_plain_aligned ();
	void object_markdown_list ();
	void object_markdown_kv_table ();
	void include_filters_drop_container_rows ();

	void scalar_array_single_column ();
	void scalar_selection_is_textual_form ();
};

void TestTextViewRenderer::academic ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Academic;

	const QString expected = QString::fromUtf8 (
		"────────────────────────────────────────────────\n"
		"id  name   description              tag  enabled\n"
		"────────────────────────────────────────────────\n"
		"0   Zero   This is the first row.   0    true\n"
		"1   One    This is the second row.  0    true\n"
		"2   Two    This is the third row.   1    true\n"
		"3   Three  This is the fourth row.  0    false\n"
		"────────────────────────────────────────────────" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::compact ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Compact;

	const QString expected = QString::fromUtf8 (
		"┌──────────────────────────────────────────────────┐\n"
		"│ id  name   description              tag  enabled │\n"
		"├──────────────────────────────────────────────────┤\n"
		"│ 0   Zero   This is the first row.   0    true    │\n"
		"│ 1   One    This is the second row.  0    true    │\n"
		"│ 2   Two    This is the third row.   1    true    │\n"
		"│ 3   Three  This is the fourth row.  0    false   │\n"
		"└──────────────────────────────────────────────────┘" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::columnar ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Columnar;

	const QString expected = QString::fromUtf8 (
		"┌────┬───────┬─────────────────────────┬─────┬─────────┐\n"
		"│ id │ name  │ description             │ tag │ enabled │\n"
		"├────┼───────┼─────────────────────────┼─────┼─────────┤\n"
		"│ 0  │ Zero  │ This is the first row.  │ 0   │ true    │\n"
		"│ 1  │ One   │ This is the second row. │ 0   │ true    │\n"
		"│ 2  │ Two   │ This is the third row.  │ 1   │ true    │\n"
		"│ 3  │ Three │ This is the fourth row. │ 0   │ false   │\n"
		"└────┴───────┴─────────────────────────┴─────┴─────────┘" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::spreadsheet ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Spreadsheet;

	const QString expected = QString::fromUtf8 (
		"┌────┬───────┬─────────────────────────┬─────┬─────────┐\n"
		"│ id │ name  │ description             │ tag │ enabled │\n"
		"├────┼───────┼─────────────────────────┼─────┼─────────┤\n"
		"│ 0  │ Zero  │ This is the first row.  │ 0   │ true    │\n"
		"├────┼───────┼─────────────────────────┼─────┼─────────┤\n"
		"│ 1  │ One   │ This is the second row. │ 0   │ true    │\n"
		"├────┼───────┼─────────────────────────┼─────┼─────────┤\n"
		"│ 2  │ Two   │ This is the third row.  │ 1   │ true    │\n"
		"├────┼───────┼─────────────────────────┼─────┼─────────┤\n"
		"│ 3  │ Three │ This is the fourth row. │ 0   │ false   │\n"
		"└────┴───────┴─────────────────────────┴─────┴─────────┘" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::minimal ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Minimal;

	const QString expected = QString::fromUtf8 (
		"id  name   description              tag  enabled\n"
		"\n"
		"0   Zero   This is the first row.   0    true\n"
		"1   One    This is the second row.  0    true\n"
		"2   Two    This is the third row.   1    true\n"
		"3   Three  This is the fourth row.  0    false" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::markdown_table ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Markdown;

	const QString expected = QString::fromUtf8 (
		"| id | name  | description             | tag | enabled |\n"
		"| -- | ----- | ----------------------- | --- | ------- |\n"
		"| 0  | Zero  | This is the first row.  | 0   | true    |\n"
		"| 1  | One   | This is the second row. | 0   | true    |\n"
		"| 2  | Two   | This is the third row.  | 1   | true    |\n"
		"| 3  | Three | This is the fourth row. | 0   | false   |" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::csv ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Csv;

	const QString expected = QStringLiteral (
		"id,name,description,tag,enabled\n"
		"0,Zero,This is the first row.,0,true\n"
		"1,One,This is the second row.,0,true\n"
		"2,Two,This is the third row.,1,true\n"
		"3,Three,This is the fourth row.,0,false" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::tsv ()
{
	TextViewProfile profile;
	profile.tableStyle = TableStyle::Tsv;

	const QString expected = QStringLiteral (
		"id\tname\tdescription\ttag\tenabled\n"
		"0\tZero\tThis is the first row.\t0\ttrue\n"
		"1\tOne\tThis is the second row.\t0\ttrue\n"
		"2\tTwo\tThis is the third row.\t1\ttrue\n"
		"3\tThree\tThis is the fourth row.\t0\tfalse" );

	QCOMPARE ( TextViewRenderer::render ( *sample_array (), profile ), expected );
}

void TestTextViewRenderer::object_plain_unaligned ()
{
	TextViewProfile profile;
	profile.alignNameSeparators = false;

	const QString expected = QStringLiteral (
		"id : 0\n"
		"name : Zero\n"
		"description : This is the first row.\n"
		"tag : 0\n"
		"enabled : true" );

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), expected );
}

void TestTextViewRenderer::object_plain_aligned ()
{
	TextViewProfile profile;
	profile.alignNameSeparators = true;

	const QString expected = QStringLiteral (
		"id          : 0\n"
		"name        : Zero\n"
		"description : This is the first row.\n"
		"tag         : 0\n"
		"enabled     : true" );

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), expected );
}

void TestTextViewRenderer::object_markdown_list ()
{
	TextViewProfile profile;
	profile.markdownListStyle = MarkdownListStyle::List;

	const QString expected = QStringLiteral (
		"- **id**: 0\n"
		"- **name**: Zero\n"
		"- **description**: This is the first row.\n"
		"- **tag**: 0\n"
		"- **enabled**: true" );

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), expected );
}

void TestTextViewRenderer::object_markdown_kv_table ()
{
	TextViewProfile profile;
	profile.markdownListStyle = MarkdownListStyle::Table;

	const QString expected = QStringLiteral (
		"| Key         | Value                  |\n"
		"| ----------- | ---------------------- |\n"
		"| id          | 0                      |\n"
		"| name        | Zero                   |\n"
		"| description | This is the first row. |\n"
		"| tag         | 0                      |\n"
		"| enabled     | true                   |" );

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), expected );
}

void TestTextViewRenderer::include_filters_drop_container_rows ()
{
	// A child object and a child array row are dropped when their include filter is off.

	std::unique_ptr<JsonNode> object = parse ( QStringLiteral (
		"{\"id\":7,\"profile\":{\"a\":1},\"roles\":[\"admin\"]}" ) );

	TextViewProfile profile;
	profile.alignNameSeparators = false;
	profile.includeObjectNames  = false;
	profile.includeArrayNames   = false;

	QCOMPARE ( TextViewRenderer::render ( *object, profile ), QStringLiteral ( "id : 7" ) );

	// With both on, the container rows show as {...} / [...].

	profile.includeObjectNames = true;
	profile.includeArrayNames  = true;

	QCOMPARE ( TextViewRenderer::render ( *object, profile ),
	           QStringLiteral ( "id : 7\nprofile : {...}\nroles : [...]" ) );
}

void TestTextViewRenderer::scalar_array_single_column ()
{
	// A scalar array renders as a single unheadered column (Compact border, no header rule).

	std::unique_ptr<JsonNode> array = parse ( QStringLiteral ( "[\"admin\",\"user\"]" ) );

	TextViewProfile profile;
	profile.tableStyle = TableStyle::Compact;

	const QString expected = QString::fromUtf8 (
		"┌───────┐\n"
		"│ admin │\n"
		"│ user  │\n"
		"└───────┘" );

	QCOMPARE ( TextViewRenderer::render ( *array, profile ), expected );
}

void TestTextViewRenderer::scalar_selection_is_textual_form ()
{
	QCOMPARE ( TextViewRenderer::render ( *JsonNode::make_number ( QStringLiteral ( "1.50" ) ), TextViewProfile () ),
	           QStringLiteral ( "1.50" ) );
	QCOMPARE ( TextViewRenderer::render ( *JsonNode::make_string ( QStringLiteral ( "hi" ) ), TextViewProfile () ),
	           QStringLiteral ( "hi" ) );
	QCOMPARE ( TextViewRenderer::render ( *JsonNode::make_null (), TextViewProfile () ),
	           QStringLiteral ( "null" ) );
}

QTEST_APPLESS_MAIN ( TestTextViewRenderer )

#include "tst_text_view_renderer.moc"

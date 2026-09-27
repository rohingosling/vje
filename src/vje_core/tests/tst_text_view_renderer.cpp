//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

	// A value long enough to wrap several times, for the SET-06 cases.

	std::unique_ptr<JsonNode> wrapping_object ()
	{
		return parse ( QStringLiteral (
			"{\"name\":\"big-test\","
			" \"textDescription\":\"Most JSON editors hand you a wall of text and leave the structure for you to hold in your head.\","
			" \"tag\":0}" ) );
	}

	// One string carrying a tab and a line break, for the SET-03 cases.

	std::unique_ptr<JsonNode> control_character_object ()
	{
		return parse ( QStringLiteral ( "{\"note\":\"A tabbed field\\t123\\nLine 2\",\"tag\":0}" ) );
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

	void string_display_modes_change_only_the_value ();
	void wrapping_hangs_the_indent_under_the_value_column ();
	void wrapping_breaks_a_word_too_long_for_the_line ();
	void wrapping_honours_line_breaks_already_in_the_value ();
	void a_markdown_list_wraps_under_its_bullet ();
	void the_machine_formats_never_wrap ();
	void wrap_columns_zero_changes_nothing ();

	void blank_lines_separate_the_entries ();
	void blank_lines_fall_between_entries_not_between_wrapped_lines ();
	void blank_lines_leave_no_trailing_blank ();
	void blank_lines_are_truly_empty ();
	void blank_lines_apply_to_the_listing_alone ();
	void blank_lines_zero_changes_nothing ();

	// Regressions for the 2026-07-28 review.

	void an_empty_value_occupies_exactly_one_line ();
	void a_wrapped_line_never_ends_in_whitespace ();
	void a_markdown_list_wraps_its_continuations_to_the_full_width ();
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

void TestTextViewRenderer::string_display_modes_change_only_the_value ()
{
	// SET-03 in the Text View: the mode reaches every rendering through cell_text, so the key-value listing shows what
	// the Form View's cells show. Nothing but the value changes -- the alignment column is computed from the KEY.

	TextViewProfile profile;

	profile.stringDisplay = StringDisplay::Escaped;

	QCOMPARE ( TextViewRenderer::render ( *control_character_object (), profile ),
	           QStringLiteral ( "note : A tabbed field\\t123\\nLine 2\n"
	                            "tag  : 0" ) );

	profile.stringDisplay = StringDisplay::Flattened;

	QCOMPARE ( TextViewRenderer::render ( *control_character_object (), profile ),
	           QStringLiteral ( "note : A tabbed field123Line 2\n"
	                            "tag  : 0" ) );

	// Decoded puts the real characters in, which is the one mode where a value occupies more than its own line.

	profile.stringDisplay = StringDisplay::Decoded;

	QCOMPARE ( TextViewRenderer::render ( *control_character_object (), profile ),
	           QStringLiteral ( "note : A tabbed field\t123\nLine 2\n"
	                            "tag  : 0" ) );
}

void TestTextViewRenderer::wrapping_hangs_the_indent_under_the_value_column ()
{
	// The shape spec section 2.10 draws: continuation lines start exactly under where the value started -- which is
	// what QPlainTextEdit's own word wrap cannot do, and the reason this lives in the renderer at all.

	TextViewProfile profile;

	profile.wrapColumns = 72;

	const QString rendered = TextViewRenderer::render ( *wrapping_object (), profile );

	QCOMPARE ( rendered, QStringLiteral (
		"name            : big-test\n"
		"textDescription : Most JSON editors hand you a wall of text and leave\n"
		"                  the structure for you to hold in your head.\n"
		"tag             : 0" ) );

	// No line exceeds the wrap column -- the claim the whole feature makes.

	for ( const QString& line : rendered.split ( QLatin1Char ( '\n' ) ) )
	{
		QVERIFY2 ( line.length () <= profile.wrapColumns,
		           qPrintable ( QStringLiteral ( "line over %1 columns: %2" ).arg ( profile.wrapColumns ).arg ( line ) ) );
	}
}

void TestTextViewRenderer::wrapping_breaks_a_word_too_long_for_the_line ()
{
	// A long URL has no space to break at. Broken at the margin rather than allowed to overrun it, or one value would
	// push the whole column sideways and defeat the setting.

	TextViewProfile profile;

	profile.wrapColumns = 30;

	const QString rendered = TextViewRenderer::render
	(
		*parse ( QStringLiteral ( "{\"url\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}" ) ),
		profile
	);

	for ( const QString& line : rendered.split ( QLatin1Char ( '\n' ) ) )
	{
		QVERIFY2 ( line.length () <= profile.wrapColumns, qPrintable ( line ) );
	}

	QVERIFY ( rendered.contains ( QLatin1Char ( '\n' ) ) );
}

void TestTextViewRenderer::wrapping_honours_line_breaks_already_in_the_value ()
{
	// Decoded mode and wrapping compose rather than fight: the value's own breaks are taken first and each resulting
	// line is wrapped independently, all under the same hanging indent.

	TextViewProfile profile;

	profile.stringDisplay = StringDisplay::Decoded;
	profile.wrapColumns   = 40;

	QCOMPARE
	(
		TextViewRenderer::render ( *parse ( QStringLiteral ( "{\"note\":\"Line 1\\nLine 2\"}" ) ), profile ),
		QStringLiteral ( "note : Line 1\n"
		                 "       Line 2" )
	);
}

void TestTextViewRenderer::a_markdown_list_wraps_under_its_bullet ()
{
	// Two spaces, not the bullet's full width: that is what Markdown reads as a continuation of the item, while a
	// deeper indent would start a code block -- so the source stays valid Markdown, which is the point of the style.

	TextViewProfile profile;

	profile.markdownListStyle = MarkdownListStyle::List;
	profile.wrapColumns       = 60;

	const QString rendered = TextViewRenderer::render ( *wrapping_object (), profile );

	QVERIFY ( rendered.contains ( QStringLiteral ( "- **textDescription**: Most JSON editors hand you a" ) ) );
	QVERIFY ( rendered.contains ( QStringLiteral ( "\n  " ) ) );
	QVERIFY ( !rendered.contains ( QStringLiteral ( "\n    " ) ) );
}

void TestTextViewRenderer::the_machine_formats_never_wrap ()
{
	// A wrapped CSV record is CORRUPT rather than untidy, and a wrapped pipe table is broken Markdown source. Asserted
	// against the unwrapped rendering of the same profile, so the claim survives a change to the wrap algorithm.

	const QList<TableStyle> machineFormats = { TableStyle::Csv, TableStyle::Tsv, TableStyle::Markdown };

	for ( const TableStyle style : machineFormats )
	{
		TextViewProfile unwrapped;
		TextViewProfile wrapped;

		unwrapped.tableStyle = style;
		wrapped.tableStyle   = style;
		wrapped.wrapColumns  = 20;

		QCOMPARE ( TextViewRenderer::render ( *sample_array (), wrapped ),
		           TextViewRenderer::render ( *sample_array (), unwrapped ) );
	}

	// The Markdown Key/Value table is the same claim on the object side.

	TextViewProfile unwrappedTable;
	TextViewProfile wrappedTable;

	unwrappedTable.markdownListStyle = MarkdownListStyle::Table;
	wrappedTable.markdownListStyle   = MarkdownListStyle::Table;
	wrappedTable.wrapColumns         = 20;

	QCOMPARE ( TextViewRenderer::render ( *wrapping_object (), wrappedTable ),
	           TextViewRenderer::render ( *wrapping_object (), unwrappedTable ) );
}

void TestTextViewRenderer::wrap_columns_zero_changes_nothing ()
{
	// The claim that made this cheap to add: with wrapping off, every rendering that predates the option is unchanged.
	// The five-row aligned listing is exactly what object_plain_aligned pinned before wrapColumns existed.

	TextViewProfile profile;

	profile.alignNameSeparators = true;
	profile.wrapColumns         = 0;

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), QStringLiteral (
		"id          : 0\n"
		"name        : Zero\n"
		"description : This is the first row.\n"
		"tag         : 0\n"
		"enabled     : true" ) );

	// And a wrap column so wide nothing reaches it is the same rendering again -- the wrap is a consequence of the
	// width, not of the flag being set.

	profile.wrapColumns = 200;

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), QStringLiteral (
		"id          : 0\n"
		"name        : Zero\n"
		"description : This is the first row.\n"
		"tag         : 0\n"
		"enabled     : true" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Blank lines between fields (SET-06)
//---------------------------------------------------------------------------------------------------------------------

void TestTextViewRenderer::blank_lines_separate_the_entries ()
{
	TextViewProfile profile;

	profile.alignNameSeparators     = true;
	profile.blankLinesBetweenFields = 1;

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), QStringLiteral (
		"id          : 0\n"
		"\n"
		"name        : Zero\n"
		"\n"
		"description : This is the first row.\n"
		"\n"
		"tag         : 0\n"
		"\n"
		"enabled     : true" ) );

	// Three is three, not "some" -- the count is the setting, so it is asserted rather than sampled.

	profile.blankLinesBetweenFields = 3;

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), QStringLiteral (
		"id          : 0\n"
		"\n\n\n"
		"name        : Zero\n"
		"\n\n\n"
		"description : This is the first row.\n"
		"\n\n\n"
		"tag         : 0\n"
		"\n\n\n"
		"enabled     : true" ) );
}

void TestTextViewRenderer::blank_lines_fall_between_entries_not_between_wrapped_lines ()
{
	// The case the implementation could plausibly get wrong. An entry is several LINES once wrapped, and the gap
	// belongs between one field and the next -- not between a field's own continuation lines, which would tear a
	// paragraph apart.

	TextViewProfile profile;

	profile.wrapColumns             = 72;
	profile.blankLinesBetweenFields = 1;

	QCOMPARE ( TextViewRenderer::render ( *wrapping_object (), profile ), QStringLiteral (
		"name            : big-test\n"
		"\n"
		"textDescription : Most JSON editors hand you a wall of text and leave\n"
		"                  the structure for you to hold in your head.\n"
		"\n"
		"tag             : 0" ) );
}

void TestTextViewRenderer::blank_lines_leave_no_trailing_blank ()
{
	// "Between", never "after". The renderer's stated contract is that there is no trailing newline, and this is the
	// obvious way to break it -- at every value, not just at one.

	for ( int blankLines = 0; blankLines <= 10; ++blankLines )
	{
		TextViewProfile profile;

		profile.blankLinesBetweenFields = blankLines;

		const QString rendered = TextViewRenderer::render ( *sample_object (), profile );

		QVERIFY2 ( !rendered.endsWith ( QLatin1Char ( '\n' ) ),
		           qPrintable ( QStringLiteral ( "trailing newline at %1 blank lines" ).arg ( blankLines ) ) );

		QVERIFY2 ( !rendered.startsWith ( QLatin1Char ( '\n' ) ),
		           qPrintable ( QStringLiteral ( "leading newline at %1 blank lines" ).arg ( blankLines ) ) );
	}
}

void TestTextViewRenderer::blank_lines_are_truly_empty ()
{
	// Not indented to the hanging indent, and carrying no trailing spaces. The Text View exists to be copied out of,
	// and trailing whitespace survives into whatever it is pasted into.

	TextViewProfile profile;

	profile.wrapColumns             = 72;
	profile.blankLinesBetweenFields = 2;

	for ( const QString& line : TextViewRenderer::render ( *wrapping_object (), profile ).split ( QLatin1Char ( '\n' ) ) )
	{
		QVERIFY2 ( line.isEmpty () || !line.trimmed ().isEmpty (),
		           qPrintable ( QStringLiteral ( "a blank line carries whitespace: \"%1\"" ).arg ( line ) ) );
	}
}

void TestTextViewRenderer::blank_lines_apply_to_the_listing_alone ()
{
	// A blank line is APPEARANCE in the key-value listing and MEANING everywhere else: it turns a tight Markdown list
	// loose, ends a Markdown table, corrupts a CSV record, and cuts through a box-drawn table's borders. Asserted
	// against the same profile at 0, so the claim survives a change to how the spacing is inserted.

	const QList<TableStyle> everyStyle =
	{
		TableStyle::Academic, TableStyle::Compact, TableStyle::Columnar, TableStyle::Spreadsheet,
		TableStyle::Minimal,  TableStyle::Markdown, TableStyle::Csv,     TableStyle::Tsv
	};

	for ( const TableStyle style : everyStyle )
	{
		TextViewProfile unspaced;
		TextViewProfile spaced;

		unspaced.tableStyle = style;
		spaced.tableStyle   = style;

		spaced.blankLinesBetweenFields = 2;

		QCOMPARE ( TextViewRenderer::render ( *sample_array (), spaced ),
		           TextViewRenderer::render ( *sample_array (), unspaced ) );
	}

	// And both Markdown object forms.

	for ( const MarkdownListStyle markdownStyle : { MarkdownListStyle::List, MarkdownListStyle::Table } )
	{
		TextViewProfile unspaced;
		TextViewProfile spaced;

		unspaced.markdownListStyle = markdownStyle;
		spaced.markdownListStyle   = markdownStyle;

		spaced.blankLinesBetweenFields = 2;

		QCOMPARE ( TextViewRenderer::render ( *sample_object (), spaced ),
		           TextViewRenderer::render ( *sample_object (), unspaced ) );
	}
}

void TestTextViewRenderer::blank_lines_zero_changes_nothing ()
{
	// The claim that made this cheap to add, and the one worth re-asserting verbatim: at 0 the listing is exactly what
	// object_plain_aligned pinned before the option existed.

	TextViewProfile profile;

	profile.alignNameSeparators     = true;
	profile.blankLinesBetweenFields = 0;

	QCOMPARE ( TextViewRenderer::render ( *sample_object (), profile ), QStringLiteral (
		"id          : 0\n"
		"name        : Zero\n"
		"description : This is the first row.\n"
		"tag         : 0\n"
		"enabled     : true" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Regressions for the 2026-07-28 review
//---------------------------------------------------------------------------------------------------------------------

void TestTextViewRenderer::an_empty_value_occupies_exactly_one_line ()
{
	// wrap_value's do-while already emits one line for an empty paragraph; a trailing isEmpty() guard emitted a second,
	// so an empty value rendered as the key prefix plus a line of pure indent whitespace. blank_lines_are_truly_empty
	// could not catch it -- its fixture has no empty value.

	TextViewProfile profile;

	profile.wrapColumns = 72;

	QCOMPARE ( TextViewRenderer::render ( *parse ( QStringLiteral ( "{\"note\":\"\",\"tag\":0}" ) ), profile ),
	           QStringLiteral ( "note :\n"
	                            "tag  : 0" ) );

	// And an interior blank line stays one blank line rather than becoming two.

	profile.stringDisplay = StringDisplay::Decoded;

	QCOMPARE ( TextViewRenderer::render ( *parse ( QStringLiteral ( "{\"note\":\"A\\n\\nB\"}" ) ), profile ),
	           QStringLiteral ( "note : A\n"
	                            "\n"
	                            "       B" ) );
}

void TestTextViewRenderer::a_wrapped_line_never_ends_in_whitespace ()
{
	// Breaking at the LAST space that fits left any run of spaces before it on the emitted line. Real prose hits this
	// on a double space after a full stop, and the Text View exists to be pasted out of.

	TextViewProfile profile;

	profile.wrapColumns = 30;

	const QString rendered = TextViewRenderer::render
	(
		*parse ( QStringLiteral ( "{\"note\":\"alpha bravo.  charlie delta.  echo foxtrot golf hotel india\"}" ) ),
		profile
	);

	for ( const QString& line : rendered.split ( QLatin1Char ( '\n' ) ) )
	{
		QVERIFY2 ( line == line.trimmed () || !line.trimmed ().isEmpty (),
		           qPrintable ( QStringLiteral ( "blank line carries whitespace: \"%1\"" ).arg ( line ) ) );

		QVERIFY2 ( !line.endsWith ( QLatin1Char ( ' ' ) ),
		           qPrintable ( QStringLiteral ( "line ends in a space: \"%1\"" ).arg ( line ) ) );
	}

	QVERIFY ( rendered.contains ( QLatin1Char ( '\n' ) ) );
}

void TestTextViewRenderer::a_markdown_list_wraps_its_continuations_to_the_full_width ()
{
	// The first line carries the bullet and the continuations carry two spaces, so the two have DIFFERENT budgets.
	// Measuring every line by the bullet left continuations short by the length of the key; measuring every line by the
	// two-space indent would have let the first line overrun. Both ends are asserted here.

	TextViewProfile profile;

	profile.markdownListStyle = MarkdownListStyle::List;
	profile.wrapColumns       = 60;

	const QStringList lines = TextViewRenderer::render ( *wrapping_object (), profile ).split ( QLatin1Char ( '\n' ) );

	for ( const QString& line : lines )
	{
		QVERIFY2 ( line.length () <= profile.wrapColumns,
		           qPrintable ( QStringLiteral ( "line over %1 columns: %2" ).arg ( profile.wrapColumns ).arg ( line ) ) );
	}

	// At least one continuation uses most of the width available to it -- the symptom was every one of them stopping
	// short by the length of the key.

	int longestContinuation = 0;

	for ( const QString& line : lines )
	{
		if ( line.startsWith ( QStringLiteral ( "  " ) ) )
		{
			longestContinuation = std::max ( longestContinuation, static_cast<int> ( line.length () ) );
		}
	}

	QVERIFY2 ( longestContinuation > profile.wrapColumns - 12,
	           qPrintable ( QStringLiteral ( "longest continuation was only %1 of %2 columns" )
	                        .arg ( longestContinuation ).arg ( profile.wrapColumns ) ) );

	// And a long key no longer disables wrapping for its entry: the bullet alone exceeds the width, but the
	// continuations have room, so the value still wraps.

	profile.wrapColumns = 48;

	const QString longKey = TextViewRenderer::render
	(
		*parse ( QStringLiteral ( "{\"aVeryLongKeyNameIndeedForThisEntry\":\"alpha bravo charlie delta echo foxtrot golf\"}" ) ),
		profile
	);

	QVERIFY2 ( longKey.contains ( QLatin1Char ( '\n' ) ), "a long key disabled wrapping for its entry" );
}

QTEST_APPLESS_MAIN ( TestTextViewRenderer )

#include "tst_text_view_renderer.moc"

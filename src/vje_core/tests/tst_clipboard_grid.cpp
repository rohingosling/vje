//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for clipboard_grid -- how text from another application is read as a block of cells (EDITOR-24).
//   HEADLESS: the whole rule is a function of a string, so it is pinned here without a clipboard, a table or a window.
//
//   The cases come in pairs where a rule has an obvious wrong twin: a JSON value spread over lines against lines of
//   text, a quote that opens a field against one that is a character, a trailing break that ends a row against one
//   that would begin an empty one.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/clipboard_grid.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestClipboardGrid : public QObject
{
	Q_OBJECT

private slots:

	void lines_from_a_text_editor_are_one_column ();
	void a_spreadsheet_range_is_rows_of_tab_separated_cells ();
	void one_trailing_break_ends_the_last_row_and_no_more ();
	void a_single_cell_copied_from_a_spreadsheet_is_a_one_cell_grid ();
	void a_single_value_with_no_break_is_not_a_grid ();
	void a_json_value_spread_over_lines_is_not_a_grid ();
	void a_quoted_field_keeps_its_tabs_breaks_and_doubled_quotes ();
	void a_quote_inside_a_field_is_a_character ();
	void an_unclosed_quote_reads_the_text_unquoted ();
	void empty_fields_and_empty_lines_are_kept_in_place ();
	void rows_may_differ_in_length_and_the_widest_sets_the_width ();

private:

	static std::vector<QStringList> grid ( const QString& text );
};

std::vector<QStringList> TestClipboardGrid::grid ( const QString& text )
{
	const std::optional<std::vector<QStringList>> rows = clipboard_grid::parse ( text );

	return rows.has_value () ? rows.value () : std::vector<QStringList> {};
}

void TestClipboardGrid::lines_from_a_text_editor_are_one_column ()
{
	// The reported case, verbatim: three lines from Notepad, which arrived as ONE cell reading Four\nFive\nSix\n.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "Four\nFive\nSix\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 3 ) );
	QCOMPARE ( rows [ 0 ], QStringList { QStringLiteral ( "Four" ) } );
	QCOMPARE ( rows [ 1 ], QStringList { QStringLiteral ( "Five" ) } );
	QCOMPARE ( rows [ 2 ], QStringList { QStringLiteral ( "Six" ) } );
	QCOMPARE ( clipboard_grid::width ( rows ), 1 );
}

void TestClipboardGrid::a_spreadsheet_range_is_rows_of_tab_separated_cells ()
{
	// Excel's own shape: CRLF between rows, a tab between cells, and a CRLF after the last row.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "a\t1\r\nb\t2\r\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 2 ) );
	QCOMPARE ( rows [ 0 ], ( QStringList { QStringLiteral ( "a" ), QStringLiteral ( "1" ) } ) );
	QCOMPARE ( rows [ 1 ], ( QStringList { QStringLiteral ( "b" ), QStringLiteral ( "2" ) } ) );
	QCOMPARE ( clipboard_grid::width ( rows ), 2 );
}

void TestClipboardGrid::one_trailing_break_ends_the_last_row_and_no_more ()
{
	// ONE is dropped, so two breaks still leave an empty last row -- the user copied a blank line, and a paste that
	// silently lost it would move every later row of a larger block by one.

	QCOMPARE ( grid ( QStringLiteral ( "x\ny\n" ) ).size (), std::size_t ( 2 ) );
	QCOMPARE ( grid ( QStringLiteral ( "x\ny\n\n" ) ).size (), std::size_t ( 3 ) );
	QCOMPARE ( grid ( QStringLiteral ( "x\ny" ) ).size (), std::size_t ( 2 ) );
}

void TestClipboardGrid::a_single_cell_copied_from_a_spreadsheet_is_a_one_cell_grid ()
{
	// Excel ends a single copied cell with a break too; read as one value it arrived as "Four\r\n", break and all.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "Four\r\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 1 ) );
	QCOMPARE ( rows [ 0 ], QStringList { QStringLiteral ( "Four" ) } );
}

void TestClipboardGrid::a_single_value_with_no_break_is_not_a_grid ()
{
	// The ordinary single-value paste is untouched: nothing to split, so no grid.

	QVERIFY ( !clipboard_grid::parse ( QStringLiteral ( "Four" ) ).has_value () );
	QVERIFY ( !clipboard_grid::parse ( QString () ).has_value () );
}

void TestClipboardGrid::a_json_value_spread_over_lines_is_not_a_grid ()
{
	// The opposing half of the first case: lines that together ARE one JSON value paste as that value, as a
	// single-line paste of it always has. A number with a trailing break is JSON too, whitespace being legal.

	QVERIFY ( !clipboard_grid::parse ( QStringLiteral ( "{\n  \"a\": 1,\n  \"b\": [ 2, 3 ]\n}\n" ) ).has_value () );
	QVERIFY ( !clipboard_grid::parse ( QStringLiteral ( "[\n1,\n2\n]" ) ).has_value () );
	QVERIFY ( !clipboard_grid::parse ( QStringLiteral ( "42\r\n" ) ).has_value () );

	// Two JSON values on two lines are not one JSON value, so they are a grid.

	QCOMPARE ( grid ( QStringLiteral ( "1\n2" ) ).size (), std::size_t ( 2 ) );
}

void TestClipboardGrid::a_quoted_field_keeps_its_tabs_breaks_and_doubled_quotes ()
{
	// Excel quotes a cell that holds a separator or a quote. The field comes back as the cell's own text.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "\"two\nlines\"\t\"say \"\"hi\"\"\"\tplain\r\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 1 ) );
	QCOMPARE ( rows [ 0 ], ( QStringList { QStringLiteral ( "two\nlines" ), QStringLiteral ( "say \"hi\"" ), QStringLiteral ( "plain" ) } ) );
}

void TestClipboardGrid::a_quote_inside_a_field_is_a_character ()
{
	// Only a quote at a field's START opens a quoted field. Anywhere else it is text -- inches, a nickname -- and
	// reading it as an opening quote would swallow the next line into this one.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "5\" pipe\nnext\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 2 ) );
	QCOMPARE ( rows [ 0 ], QStringList { QStringLiteral ( "5\" pipe" ) } );
	QCOMPARE ( rows [ 1 ], QStringList { QStringLiteral ( "next" ) } );

	// And where a second mid-field quote CLOSES the first, so the unclosed-quote fallback never comes into it. Read
	// with a quote honoured anywhere, the break between the two lines vanishes into one field -- the input above
	// cannot show that, since its stray quote never closes and the fallback reads the text again unquoted.

	const std::vector<QStringList> paired = grid ( QStringLiteral ( "a\"b\nc\"d\n" ) );

	QCOMPARE ( paired.size (), std::size_t ( 2 ) );
	QCOMPARE ( paired [ 0 ], QStringList { QStringLiteral ( "a\"b" ) } );
	QCOMPARE ( paired [ 1 ], QStringList { QStringLiteral ( "c\"d" ) } );
}

void TestClipboardGrid::an_unclosed_quote_reads_the_text_unquoted ()
{
	// A quote at a field's start that never closes was not quoting anything. Read with quoting on, the rest of the
	// paste would vanish into one field; read again with it off, every line keeps its place.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "\"open\nsecond\nthird\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 3 ) );
	QCOMPARE ( rows [ 0 ], QStringList { QStringLiteral ( "\"open" ) } );
	QCOMPARE ( rows [ 2 ], QStringList { QStringLiteral ( "third" ) } );
}

void TestClipboardGrid::empty_fields_and_empty_lines_are_kept_in_place ()
{
	// An empty cell in a copied range holds its column, and an empty line holds its row -- which the paste needs to
	// land every other value where the user saw it.

	const std::vector<QStringList> rows = grid ( QStringLiteral ( "a\t\tc\n\nx\t\t\n" ) );

	QCOMPARE ( rows.size (), std::size_t ( 3 ) );
	QCOMPARE ( rows [ 0 ], ( QStringList { QStringLiteral ( "a" ), QString (), QStringLiteral ( "c" ) } ) );
	QCOMPARE ( rows [ 1 ], QStringList { QString () } );
	QCOMPARE ( rows [ 2 ], ( QStringList { QStringLiteral ( "x" ), QString (), QString () } ) );
}

void TestClipboardGrid::rows_may_differ_in_length_and_the_widest_sets_the_width ()
{
	const std::vector<QStringList> rows = grid ( QStringLiteral ( "a\nb\tc\td\ne\tf\n" ) );

	QCOMPARE ( rows [ 0 ].size (), 1 );
	QCOMPARE ( rows [ 1 ].size (), 3 );
	QCOMPARE ( rows [ 2 ].size (), 2 );
	QCOMPARE ( clipboard_grid::width ( rows ), 3 );
}

QTEST_GUILESS_MAIN ( TestClipboardGrid )

#include "tst_clipboard_grid.moc"

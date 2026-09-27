//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for print_html -- how a view's rendering becomes a printed page (FILE-12).
//
//   THIS IS WHERE THE APPEARANCE OF A PRINTED PAGE IS PINNED, and it runs in the HEADLESS harness: no printer, no
//   QApplication, no paper. That it can is the phase's design stated as a build rule -- everything about how the page
//   LOOKS is a pure function of the content and two font names, and everything a printer is needed for (pagination,
//   furniture, the job itself) is PrintController's and is covered separately.
//
//   The cases that matter are the ones a reader would notice and could not recover from: text escaped so a JSON string
//   containing "<" does not vanish into a tag, an over-wide line wrapped rather than cut off at the margin, a ragged
//   table still drawn as a rectangle, and a page that is black on white whatever theme the screen was in.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "printing/print_html.hpp"

#include <QtTest/QtTest>

#include <QString>
#include <QStringList>

using namespace vje;

class TestPrintHtml : public QObject
{
	Q_OBJECT

private slots:

	void nothing_to_print_renders_nothing ();
	void preformatted_text_is_set_in_the_fixed_family ();
	void an_over_wide_preformatted_line_wraps_rather_than_being_cut ();
	void a_rendering_that_declares_clip_is_not_wrapped_by_the_page ();
	void preformatted_text_is_escaped_and_its_alignment_is_kept ();
	void a_table_carries_its_headers ();
	void the_object_form_prints_no_header_row ();
	void a_ragged_row_is_padded_to_the_full_column_count ();
	void an_empty_cell_still_draws_its_box ();
	void a_line_break_inside_a_cell_stays_a_line_break ();
	void the_page_is_black_on_white_whatever_the_theme ();
	void a_family_name_cannot_break_out_of_the_style ();

private:

	static PrintStyle style ();

	static PrintContent preformatted ( const QString& text );
	static PrintContent table ( const QStringList& headers, const QList<QStringList>& rows );
};

//=====================================================================================================================
// Helpers
//=====================================================================================================================

PrintStyle TestPrintHtml::style ()
{
	PrintStyle chosen;

	// Deliberately distinguishable names and sizes, so a case asserting "the fixed family was used" cannot pass by
	// accident against a build that reached for the body one.

	chosen.bodyFamily     = QStringLiteral ( "Body Family" );
	chosen.fixedFamily    = QStringLiteral ( "Fixed Family" );
	chosen.bodyPointSize  = 11;
	chosen.fixedPointSize = 7;

	return chosen;
}

PrintContent TestPrintHtml::preformatted ( const QString& text )
{
	PrintContent content;

	content.kind     = PrintContent::Kind::Preformatted;
	content.viewName = QStringLiteral ( "Text View" );
	content.text     = text;

	return content;
}

PrintContent TestPrintHtml::table ( const QStringList& headers, const QList<QStringList>& rows )
{
	PrintContent content;

	content.kind     = PrintContent::Kind::Table;
	content.viewName = QStringLiteral ( "Form View" );
	content.headers  = headers;
	content.rows     = rows;

	return content;
}

//=====================================================================================================================
// Cases
//=====================================================================================================================

void TestPrintHtml::nothing_to_print_renders_nothing ()
{
	// A view with nothing to show yields no page at all, rather than an empty one -- which is what lets the controller
	// tell the user there was nothing to print instead of feeding a blank sheet through the printer.

	QVERIFY ( print_html ( PrintContent (), style () ).isEmpty () );
}

void TestPrintHtml::preformatted_text_is_set_in_the_fixed_family ()
{
	// The load-bearing one. TextViewRenderer aligns its columns and draws its table rules by COUNTING CHARACTERS, so a
	// proportional family here leaves every rule broken and every column ragged (EDITOR-06).

	const QString html = print_html ( preformatted ( QStringLiteral ( "a  b" ) ), style () );

	QVERIFY2 ( html.contains ( QStringLiteral ( "'Fixed Family'" ) ), qPrintable ( html ) );
	QVERIFY2 ( html.contains ( QStringLiteral ( "font-size:7pt" ) ),  qPrintable ( html ) );

	QVERIFY2 ( !html.contains ( QStringLiteral ( "'Body Family'" ) ), qPrintable ( html ) );
}

void TestPrintHtml::a_rendering_that_declares_clip_is_not_wrapped_by_the_page ()
{
	// CSV, TSV, the Markdown table and the five box styles are renderings where a broken line is a CORRUPT record
	// rather than an untidy one. The view says which it is producing; this is the page honouring that, and it is the
	// one place the default -- never lose text -- is deliberately given up.

	PrintContent content = preformatted ( QStringLiteral ( "a,b,c" ) );

	content.overflow = PrintContent::Overflow::Clip;

	const QString html = print_html ( content, style () );

	QVERIFY2 ( html.contains ( QStringLiteral ( "white-space:pre;" ) ), qPrintable ( html ) );

	QVERIFY2 ( !html.contains ( QStringLiteral ( "pre-wrap" ) ), qPrintable ( html ) );
}

void TestPrintHtml::an_over_wide_preformatted_line_wraps_rather_than_being_cut ()
{
	// A clipped line loses text the reader cannot see going and cannot get back; a wrapped one loses only the
	// alignment of the single line that overflowed. The rendered consequence is asserted in tst_print_controller,
	// which has a layout to measure; this pins the declaration that produces it.

	const QString html = print_html ( preformatted ( QStringLiteral ( "x" ) ), style () );

	QVERIFY2 ( html.contains ( QStringLiteral ( "white-space:pre-wrap" ) ), qPrintable ( html ) );
}

void TestPrintHtml::preformatted_text_is_escaped_and_its_alignment_is_kept ()
{
	const QString html = print_html ( preformatted ( QStringLiteral ( "a <b> & \"c\"\n  indented" ) ), style () );

	QVERIFY2 ( html.contains ( QStringLiteral ( "a &lt;b&gt; &amp;" ) ), qPrintable ( html ) );

	// The escaping must not touch the SPACES: they are the alignment, and a renderer that collapsed them would break
	// the very thing the fixed-width font is there to preserve.

	QVERIFY2 ( html.contains ( QStringLiteral ( "\n  indented" ) ), qPrintable ( html ) );
}

void TestPrintHtml::a_table_carries_its_headers ()
{
	const QString html = print_html
	(
		table ( { QStringLiteral ( "name" ), QStringLiteral ( "age" ) },
		        { { QStringLiteral ( "Ada" ), QStringLiteral ( "36" ) } } ),
		style ()
	);

	QVERIFY2 ( html.contains ( QStringLiteral ( "<th align=\"left\">name</th>" ) ), qPrintable ( html ) );
	QVERIFY2 ( html.contains ( QStringLiteral ( "<th align=\"left\">age</th>" ) ),  qPrintable ( html ) );
	QVERIFY2 ( html.contains ( QStringLiteral ( "<td>Ada</td>" ) ),                 qPrintable ( html ) );
}

void TestPrintHtml::the_object_form_prints_no_header_row ()
{
	// The object form's key / value columns are unlabelled on screen (EDITOR-02), so they stay unlabelled on paper --
	// an invented "Key" / "Value" row would be the printer saying something the view never did.

	const QString html = print_html
	(
		table ( QStringList (), { { QStringLiteral ( "name" ), QStringLiteral ( "Ada" ) } } ),
		style ()
	);

	QVERIFY2 ( !html.contains ( QStringLiteral ( "<th" ) ), qPrintable ( html ) );

	QVERIFY2 ( html.contains ( QStringLiteral ( "<td>name</td><td>Ada</td>" ) ), qPrintable ( html ) );
}

void TestPrintHtml::a_ragged_row_is_padded_to_the_full_column_count ()
{
	// EDITOR-03's key union puts a MISSING cell in every element that lacks a member. A row printed short would slide
	// its remaining cells left under the wrong headings, which is worse than an empty cell: it is wrong rather than
	// merely absent.

	const QString html = print_html
	(
		table ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ), QStringLiteral ( "c" ) },
		        { { QStringLiteral ( "1" ) } } ),
		style ()
	);

	QVERIFY2 ( html.contains ( QStringLiteral ( "<td>1</td><td>&nbsp;</td><td>&nbsp;</td>" ) ), qPrintable ( html ) );
}

void TestPrintHtml::an_empty_cell_still_draws_its_box ()
{
	const QString html = print_html
	(
		table ( { QStringLiteral ( "a" ) }, { { QString () } } ),
		style ()
	);

	QVERIFY2 ( html.contains ( QStringLiteral ( "<td>&nbsp;</td>" ) ), qPrintable ( html ) );
}

void TestPrintHtml::a_line_break_inside_a_cell_stays_a_line_break ()
{
	// SET-03's Decoded notation puts real line breaks into a value, and HTML runs those lines together unless they
	// are made into breaks -- so a two-line string would print as one line with a space where the newline was.

	const QString html = print_html
	(
		table ( { QStringLiteral ( "a" ) }, { { QStringLiteral ( "one\ntwo" ) } } ),
		style ()
	);

	QVERIFY2 ( html.contains ( QStringLiteral ( "one<br/>two" ) ), qPrintable ( html ) );
}

void TestPrintHtml::the_page_is_black_on_white_whatever_the_theme ()
{
	// Not a preference. ThemeService's Dark palette draws near-white text; a page that took its colour from the screen
	// would come out of the printer blank. The rendering therefore states its own colour and takes none from anywhere.

	const QString preformattedHtml = print_html ( preformatted ( QStringLiteral ( "x" ) ), style () );
	const QString tableHtml        = print_html ( table ( { QStringLiteral ( "a" ) }, { { QStringLiteral ( "1" ) } } ), style () );

	QVERIFY2 ( preformattedHtml.contains ( QStringLiteral ( "color:#000000" ) ), qPrintable ( preformattedHtml ) );
	QVERIFY2 ( tableHtml.contains        ( QStringLiteral ( "color:#000000" ) ), qPrintable ( tableHtml ) );

	// And no background, so the paper is the paper. A dark fill would be printed.

	QVERIFY2 ( !preformattedHtml.contains ( QStringLiteral ( "background" ) ), qPrintable ( preformattedHtml ) );
	QVERIFY2 ( !tableHtml.contains        ( QStringLiteral ( "background" ) ), qPrintable ( tableHtml ) );
}

void TestPrintHtml::a_family_name_cannot_break_out_of_the_style ()
{
	// The families come from the platform's font database rather than from us, so the one character that could end the
	// quoted CSS value early is removed rather than trusted not to appear.

	PrintStyle awkward = style ();

	awkward.fixedFamily = QStringLiteral ( "Od'd" );

	const QString html = print_html ( preformatted ( QStringLiteral ( "x" ) ), awkward );

	// The quote is removed rather than escaped, so what reaches the CSS is one quoted run and nothing after it can be
	// read as a further declaration.

	QVERIFY2 ( html.contains ( QStringLiteral ( "font-family:'Odd';" ) ), qPrintable ( html ) );
}

QTEST_APPLESS_MAIN ( TestPrintHtml )

#include "tst_print_html.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for wrap_preformatted -- how a line too wide for the page is broken (FILE-12).
//
//   THE WHOLE CLAIM IS THE HANGING INDENT. Breaking a line is easy and Qt would do it for nothing; what it will not do
//   is put the continuation anywhere but column 0, and for the Code View that loses the one thing a JSON line's
//   position on the page tells the reader. A continuation at column 0 reads as a sibling at the document root.
//
//   Runs in the HEADLESS harness: a wrap is arithmetic over a string, and there is no printer, no font and no widget
//   anywhere in it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "printing/print_wrapping.hpp"

#include <QtTest/QtTest>

#include <QString>
#include <QStringList>

using namespace vje;

class TestPrintWrapping : public QObject
{
	Q_OBJECT

private slots:

	void a_line_that_fits_is_left_exactly_as_it_was ();
	void an_unknown_width_wraps_nothing ();
	void a_continuation_sits_under_its_own_line_s_indent ();
	void an_unindented_line_continues_at_column_zero ();
	void no_emitted_line_exceeds_the_width ();
	void the_break_is_taken_at_a_space_and_the_space_is_consumed ();
	void an_unbreakable_token_is_broken_at_the_margin ();
	void a_line_whose_indent_leaves_no_room_is_left_long ();
	void blank_lines_and_line_count_are_preserved_where_nothing_wraps ();
	void no_emitted_line_ends_in_whitespace ();

private:

	static QStringList lines_of ( const QString& text );

	// A line of `indent` spaces followed by `words` words of `wordLength` characters, so a case can state the width it
	// wants to overflow rather than counting characters in a literal.

	static QString built_line ( int indent, int words, int wordLength );
};

//=====================================================================================================================
// Helpers
//=====================================================================================================================

QStringList TestPrintWrapping::lines_of ( const QString& text )
{
	return text.split ( QLatin1Char ( '\n' ) );
}

QString TestPrintWrapping::built_line ( int indent, int words, int wordLength )
{
	QStringList parts;

	for ( int index = 0; index < words; ++index )
	{
		parts << QString ( wordLength, QLatin1Char ( 'a' + ( index % 26 ) ) );
	}

	return QString ( indent, QLatin1Char ( ' ' ) ) + parts.join ( QLatin1Char ( ' ' ) );
}

//=====================================================================================================================
// Cases
//=====================================================================================================================

void TestPrintWrapping::a_line_that_fits_is_left_exactly_as_it_was ()
{
	const QString text = QStringLiteral ( "    \"count\": 0," );

	QCOMPARE ( wrap_preformatted ( text, 80 ), text );
}

void TestPrintWrapping::an_unknown_width_wraps_nothing ()
{
	// 0 is the caller saying it does not know the page width, which is what a headless test of a view produces. The
	// honest answer is the text unchanged rather than a guess at a width.

	const QString text = built_line ( 4, 40, 9 );

	QCOMPARE ( wrap_preformatted ( text, 0 ),  text );
	QCOMPARE ( wrap_preformatted ( text, -1 ), text );
}

void TestPrintWrapping::a_continuation_sits_under_its_own_line_s_indent ()
{
	// The claim the whole helper exists for. A JSON line's meaning is read off its indentation, so a continuation that
	// started at column 0 would read as a sibling at the document root.

	const QString wrapped = wrap_preformatted ( built_line ( 8, 20, 9 ), 60 );
	const QStringList lines = lines_of ( wrapped );

	QVERIFY2 ( lines.size () > 1, qPrintable ( wrapped ) );

	for ( int index = 1; index < lines.size (); ++index )
	{
		QVERIFY2
		(
			lines.at ( index ).startsWith ( QString ( 8, QLatin1Char ( ' ' ) ) ),
			qPrintable ( QStringLiteral ( "continuation %1 is not under the indent: '%2'" ).arg ( index ).arg ( lines.at ( index ) ) )
		);

		// And it is EXACTLY the indent -- a continuation pushed one column further would drift down the page.

		QVERIFY2
		(
			!lines.at ( index ).startsWith ( QString ( 9, QLatin1Char ( ' ' ) ) ),
			qPrintable ( lines.at ( index ) )
		);
	}
}

void TestPrintWrapping::an_unindented_line_continues_at_column_zero ()
{
	// The rule is the line's OWN indent, so a line that begins at the margin continues at the margin. Nothing is
	// invented for it.

	const QStringList lines = lines_of ( wrap_preformatted ( built_line ( 0, 20, 9 ), 60 ) );

	QVERIFY ( lines.size () > 1 );

	QVERIFY2 ( !lines.at ( 1 ).startsWith ( QLatin1Char ( ' ' ) ), qPrintable ( lines.at ( 1 ) ) );
}

void TestPrintWrapping::no_emitted_line_exceeds_the_width ()
{
	constexpr int COLUMNS = 60;

	const QStringList lines = lines_of ( wrap_preformatted ( built_line ( 8, 30, 9 ), COLUMNS ) );

	for ( const QString& line : lines )
	{
		QVERIFY2
		(
			line.size () <= COLUMNS,
			qPrintable ( QStringLiteral ( "%1 characters: '%2'" ).arg ( line.size () ).arg ( line ) )
		);
	}
}

void TestPrintWrapping::the_break_is_taken_at_a_space_and_the_space_is_consumed ()
{
	// Words are kept whole, and the space the break was taken at does not reappear at the head of the next line --
	// which would push every continuation one column past the indent it is meant to sit under.

	const QStringList lines = lines_of ( wrap_preformatted ( built_line ( 4, 12, 9 ), 40 ) );

	QVERIFY ( lines.size () > 1 );

	for ( int index = 1; index < lines.size (); ++index )
	{
		QVERIFY2
		(
			!lines.at ( index ).startsWith ( QString ( 5, QLatin1Char ( ' ' ) ) ),
			qPrintable ( lines.at ( index ) )
		);
	}

	// Every word survives intact: joining the pieces back up recovers the original words in order.

	const QString rejoined = lines.join ( QLatin1Char ( ' ' ) ).simplified ();

	QCOMPARE ( rejoined, built_line ( 4, 12, 9 ).simplified () );
}

void TestPrintWrapping::an_unbreakable_token_is_broken_at_the_margin ()
{
	// A base64 value or a long URL has no space to break at, and the alternative to breaking it is a line that runs
	// off the paper -- taking the reader's place with it, since the page clips rather than scrolls.

	constexpr int COLUMNS = 40;

	const QString text  = QStringLiteral ( "    " ) + QString ( 200, QLatin1Char ( 'x' ) );
	const QStringList lines = lines_of ( wrap_preformatted ( text, COLUMNS ) );

	QVERIFY ( lines.size () > 1 );

	for ( const QString& line : lines )
	{
		QVERIFY2 ( line.size () <= COLUMNS, qPrintable ( QString::number ( line.size () ) ) );
	}

	// Nothing was lost on the way: every 'x' is still there.

	QCOMPARE ( lines.join ( QString () ).count ( QLatin1Char ( 'x' ) ), 200 );
}

void TestPrintWrapping::a_line_whose_indent_leaves_no_room_is_left_long ()
{
	// Below a handful of characters a wrap produces a column of syllables rather than a paragraph, and an over-long
	// line is the better of the two. Left long, and therefore left for the page's own pre-wrap to break -- which loses
	// the alignment but not the text.

	const QString text = QString ( 38, QLatin1Char ( ' ' ) ) + QStringLiteral ( "alpha bravo charlie delta echo" );

	QCOMPARE ( wrap_preformatted ( text, 40 ), text );
}

void TestPrintWrapping::blank_lines_and_line_count_are_preserved_where_nothing_wraps ()
{
	// The identity case, and the one that matters most in practice: a document whose lines all fit comes back
	// character for character, blank lines and all.

	const QString text = QStringLiteral ( "{\n\n    \"a\": 1,\n\n    \"b\": 2\n}" );

	QCOMPARE ( wrap_preformatted ( text, 80 ), text );
}

void TestPrintWrapping::no_emitted_line_ends_in_whitespace ()
{
	// Breaking at the LAST space that fits leaves any run of spaces before it on the emitted line. TextViewRenderer is
	// careful about this for the same reason, and a printed line ending in invisible whitespace is a line whose right
	// edge does not mean what it looks like.

	const QString text = QStringLiteral ( "    " ) + QStringLiteral ( "alpha  bravo  charlie  delta  echo  foxtrot  golf  hotel" );

	for ( const QString& line : lines_of ( wrap_preformatted ( text, 30 ) ) )
	{
		QVERIFY2 ( !line.endsWith ( QLatin1Char ( ' ' ) ), qPrintable ( QStringLiteral ( "'%1'" ).arg ( line ) ) );
	}
}

QTEST_APPLESS_MAIN ( TestPrintWrapping )

#include "tst_print_wrapping.moc"

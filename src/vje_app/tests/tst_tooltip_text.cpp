//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for tooltip_text, STYLE-17's one tooltip formatter: short text untouched, long text wrapped at a
//   space and inside a run with none, the breaks it was given kept, the length capped with an ellipsis (including when
//   the input itself is cut short), and text Qt would sniff as markup shown as written. The markup case is checked by
//   round-tripping through QTextDocument -- what QToolTip renders with -- rather than by comparing strings, so it
//   asserts what the user sees rather than how it is spelled.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/tooltip_text.hpp"

#include "AppConfig.hpp"

#include <QTextDocument>
#include <QtTest/QtTest>

using namespace vje;

namespace
{
	constexpr int LINE_LENGTH   = config::tooltip::LINE_LENGTH;
	constexpr int MAXIMUM_LINES = config::tooltip::MAXIMUM_LINES;

	// The plain text a tooltip's string renders as -- through the same sniff QToolTip applies.

	QString rendered ( const QString& tooltip )
	{
		if ( !Qt::mightBeRichText ( tooltip ) )
		{
			return tooltip;
		}

		QTextDocument document;

		document.setHtml ( tooltip );

		return document.toPlainText ();
	}
}

class TestTooltipText : public QObject
{
	Q_OBJECT

private slots:

	void empty_text_is_no_tooltip ();
	void short_text_is_unchanged ();
	void long_text_wraps_at_the_last_space_that_fits ();
	void a_run_with_no_space_breaks_at_the_measure ();
	void the_breaks_it_was_given_are_kept ();
	void a_long_text_is_capped_with_an_ellipsis ();
	void an_enormous_value_is_capped_without_being_read_in_full ();
	void markup_is_shown_as_written ();
	void wrapped_markup_keeps_its_breaks ();
};

void TestTooltipText::empty_text_is_no_tooltip ()
{
	QCOMPARE ( tooltip_text ( QString () ), QString () );
}

void TestTooltipText::short_text_is_unchanged ()
{
	const QString text = QStringLiteral ( "Save (Ctrl+S)" );

	QCOMPARE ( tooltip_text ( text ), text );

	// Exactly the measure is not over it.

	const QString full ( LINE_LENGTH, QLatin1Char ( 'x' ) );

	QCOMPARE ( tooltip_text ( full ), full );
}

void TestTooltipText::long_text_wraps_at_the_last_space_that_fits ()
{
	// Words of five letters and a space: the last space at or before the measure is where the line breaks, and the
	// space itself is dropped rather than starting the next line.

	QString text;

	while ( text.size () < ( LINE_LENGTH * 2 ) )
	{
		text += QStringLiteral ( "abcde " );
	}

	text = text.trimmed ();

	const QStringList lines = tooltip_text ( text ).split ( QLatin1Char ( '\n' ) );

	QVERIFY ( lines.size () >= 2 );

	for ( const QString& line : lines )
	{
		QVERIFY2 ( line.size () <= LINE_LENGTH, qPrintable ( line ) );
		QVERIFY2 ( !line.startsWith ( QLatin1Char ( ' ' ) ) && !line.endsWith ( QLatin1Char ( ' ' ) ), qPrintable ( line ) );
		QVERIFY2 ( line.endsWith ( QStringLiteral ( "abcde" ) ), qPrintable ( line ) );   // No word was split.
	}

	QCOMPARE ( lines.join ( QLatin1Char ( ' ' ) ), text );                             // Nothing lost but the break spaces.
}

void TestTooltipText::a_run_with_no_space_breaks_at_the_measure ()
{
	const QString run ( ( LINE_LENGTH * 2 ) + 5, QLatin1Char ( 'q' ) );

	const QStringList lines = tooltip_text ( run ).split ( QLatin1Char ( '\n' ) );

	QCOMPARE ( lines.size (), 3 );
	QCOMPARE ( lines.at ( 0 ).size (), LINE_LENGTH );
	QCOMPARE ( lines.at ( 1 ).size (), LINE_LENGTH );
	QCOMPARE ( lines.at ( 2 ).size (), 5 );
}

void TestTooltipText::the_breaks_it_was_given_are_kept ()
{
	// A tree node's tooltip is a pointer, then a type, then a value: three lines by construction. And a Windows line
	// ending in a value does not leave a stray carriage return on the screen.

	QCOMPARE ( tooltip_text ( QStringLiteral ( "/a/b\nstring\r\n\"x\"" ) ), QStringLiteral ( "/a/b\nstring\n\"x\"" ) );
}

void TestTooltipText::a_long_text_is_capped_with_an_ellipsis ()
{
	QStringList many;

	for ( int line = 0; line < ( MAXIMUM_LINES + 5 ); ++line )
	{
		many.append ( QStringLiteral ( "line %1" ).arg ( line ) );
	}

	const QStringList lines = tooltip_text ( many.join ( QLatin1Char ( '\n' ) ) ).split ( QLatin1Char ( '\n' ) );

	QCOMPARE ( lines.size (), MAXIMUM_LINES );
	QCOMPARE ( lines.first (), QStringLiteral ( "line 0" ) );
	QCOMPARE ( lines.last (), QStringLiteral ( "line %1" ).arg ( MAXIMUM_LINES - 1 ) + QChar ( 0x2026 ) );

	// And the ellipsis stays inside the measure when the last kept line is already full.

	const QString run ( LINE_LENGTH * ( MAXIMUM_LINES + 1 ), QLatin1Char ( 'z' ) );

	const QStringList runLines = tooltip_text ( run ).split ( QLatin1Char ( '\n' ) );

	QCOMPARE ( runLines.size (), MAXIMUM_LINES );
	QCOMPARE ( runLines.last ().size (), LINE_LENGTH );
	QVERIFY  ( runLines.last ().endsWith ( QChar ( 0x2026 ) ) );
}

void TestTooltipText::an_enormous_value_is_capped_without_being_read_in_full ()
{
	// A megabyte on one line with spaces in it: capped like any other, ending in the ellipsis. The bound on the work is
	// a bound on the INPUT read, which this cannot observe directly -- but a formatter that read it all and wrapped
	// it would still produce this answer, only slowly, so the time is what the case would show; it is the answer that
	// is asserted.

	QString huge;

	huge.reserve ( 1 << 20 );

	while ( huge.size () < ( 1 << 20 ) )
	{
		huge += QStringLiteral ( "word " );
	}

	const QString tooltip = tooltip_text ( huge );

	QCOMPARE ( tooltip.split ( QLatin1Char ( '\n' ) ).size (), MAXIMUM_LINES );
	QVERIFY  ( tooltip.endsWith ( QChar ( 0x2026 ) ) );
	QVERIFY  ( tooltip.size () <= ( MAXIMUM_LINES * ( LINE_LENGTH + 1 ) ) );

	// And a value cut exactly by the read bound, on a line of its own, still says it was cut.

	const QString cut = QString ( LINE_LENGTH, QLatin1Char ( 'a' ) ) + QLatin1Char ( '\n' ) + QString ( MAXIMUM_LINES * ( LINE_LENGTH + 1 ), QLatin1Char ( 'b' ) );

	QVERIFY ( tooltip_text ( cut ).endsWith ( QChar ( 0x2026 ) ) );
}

void TestTooltipText::markup_is_shown_as_written ()
{
	// A JSON string holding markup is a value, not a format. Handed to Qt as it stands it renders bold and loses its
	// tags; through the formatter it reads back exactly as written.

	const QString value = QStringLiteral ( "<b>bold</b> & <i>not</i>" );

	QVERIFY  ( Qt::mightBeRichText ( value ) );                          // The hazard is real for this input.
	QVERIFY  ( rendered ( value ) != value );                            // And it is what Qt would show unaided.

	QCOMPARE ( rendered ( tooltip_text ( value ) ), value );
}

void TestTooltipText::wrapped_markup_keeps_its_breaks ()
{
	QString value = QStringLiteral ( "<p>" );

	while ( value.size () < ( LINE_LENGTH * 2 ) )
	{
		value += QStringLiteral ( "text " );
	}

	value += QStringLiteral ( "</p>" );

	const QString tooltip = tooltip_text ( value );

	QVERIFY ( Qt::mightBeRichText ( tooltip ) );

	const QStringList lines = rendered ( tooltip ).split ( QLatin1Char ( '\n' ) );

	QVERIFY ( lines.size () >= 2 );
	QVERIFY ( lines.first ().startsWith ( QStringLiteral ( "<p>" ) ) );
	QVERIFY ( lines.last ().endsWith ( QStringLiteral ( "</p>" ) ) );

	for ( const QString& line : lines )
	{
		QVERIFY2 ( line.size () <= LINE_LENGTH, qPrintable ( line ) );
	}
}

QTEST_MAIN ( TestTooltipText )

#include "tst_tooltip_text.moc"

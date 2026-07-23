//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonHighlighter and CodeTokenPalette -- the Code View's syntax colouring
//   (EDITOR-07).
//
//   WHAT IS ACTUALLY ASSERTED IS THE RENDERED FORMAT, read back out of the QTextDocument character by character, not
//   the highlighter's internal opinion about a token. The reason: a colour claim checked against a
//   reference built differently from the way the application builds it can agree with a wrong answer for a full round
//   while looking entirely plausible. Reading the document's own formats is the one reference that cannot be wrong the
//   same way the code is.
//
//   The claims:
//
//     - MEMBER NAMES AND STRING VALUES ARE DIFFERENT COLOURS, and the distinction is made by lookahead to the ':'.
//       This is the single most useful thing the colouring does and the only part of it that needs context.
//     - EVERY TOKEN KIND GETS ITS COLOUR -- numbers, the three literals, and structural punctuation.
//     - AN INVALID TAIL DOES NOT PAINT the document red. Text is invalid for most of the time a user spends typing.
//     - THE PALETTE IS MEASURED FROM QPalette::Base, so light and dark are answered by the surface the colours are read
//       against rather than by a setting -- and the derived chrome moves AWAY from that surface under both themes,
//       which is the failure the splitter grip actually shipped with (STYLE-04).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/CodeTokenPalette.hpp"
#include "views/JsonHighlighter.hpp"

#include <QtTest/QtTest>

#include <QPalette>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <memory>

using namespace vje;

namespace
{
	QPalette palette_with_base ( const QColor& base )
	{
		QPalette palette;

		palette.setColor ( QPalette::Base, base );

		return palette;
	}

	const QColor LIGHT_BASE = QColor ( 0xFF, 0xFF, 0xFF );
	const QColor DARK_BASE  = QColor ( 0x1E, 0x1E, 0x1E );
}

class TestJsonHighlighter : public QObject
{
	Q_OBJECT

private:

	// Set the text and drive the highlighting pass, exactly as CodeView::refresh_from_document does.
	//
	// The explicit rehighlight is not test scaffolding. QSyntaxHighlighter schedules its initial pass on a zero-timer,
	// so a buffer that has just been replaced carries NO format ranges until the event loop gets a turn -- measured,
	// not assumed. The view calls rehighlight() after replacing its buffer for that reason, and this mirrors it, so
	// what is under test is the path the application actually takes.

	void set_text ( QTextDocument& document, JsonHighlighter& highlighter, const QString& text )
	{
		document.setPlainText ( text );

		highlighter.rehighlight ();
	}

	// The colour actually painted at a character offset, as the widget would show it. An invalid colour means nothing
	// is painted there.
	//
	// READ FROM THE BLOCK'S LAYOUT, not from its text fragments -- and that distinction is the trap this helper exists
	// to avoid. A QSyntaxHighlighter does NOT modify the document's character formats; it publishes an overlay through
	// QTextLayout::setFormats. Asking a fragment for its charFormat therefore returns the UNDERLYING format, which for
	// plain text is a default-constructed brush whose colour reads as opaque black -- a perfectly plausible-looking
	// answer that has nothing to do with what the highlighter did (build the reference the way the
	// application does).

	QColor colour_at ( const QTextDocument& document, int position )
	{
		const QTextBlock block = document.findBlock ( position );

		if ( !block.isValid () || ( block.layout () == nullptr ) )
		{
			return QColor ();
		}

		const int offsetInBlock = position - block.position ();

		for ( const QTextLayout::FormatRange& range : block.layout ()->formats () )
		{
			if ( ( offsetInBlock >= range.start ) && ( offsetInBlock < ( range.start + range.length ) ) )
			{
				return range.format.foreground ().color ();
			}
		}

		return QColor ();
	}

private slots:

	//=================================================================================================================
	// Token classification.
	//=================================================================================================================

	void a_member_name_and_a_string_value_are_different_colours ()
	{
		QTextDocument document;

		JsonHighlighter highlighter ( &document );

		const CodeTokenPalette tokens = CodeTokenPalette::for_palette ( palette_with_base ( LIGHT_BASE ) );

		highlighter.set_token_palette ( tokens );

		//                                 0123456789...
		set_text ( document, highlighter, QStringLiteral ( "{ \"name\": \"Alex\" }" ) );

		// Offset 2 is inside the key's opening quote; offset 11 is inside the value's.

		QCOMPARE ( colour_at ( document,  2 ), tokens.memberName );
		QCOMPARE ( colour_at ( document, 11 ), tokens.stringValue );

		QVERIFY2 ( tokens.memberName != tokens.stringValue,
		           "telling a key from a value at a glance is the whole value of the colouring" );
	}

	void a_string_element_of_an_array_is_a_value_not_a_name ()
	{
		QTextDocument document;

		JsonHighlighter highlighter ( &document );

		const CodeTokenPalette tokens = CodeTokenPalette::for_palette ( palette_with_base ( DARK_BASE ) );

		highlighter.set_token_palette ( tokens );

		set_text ( document, highlighter, QStringLiteral ( "[ \"admin\" ]" ) );

		QCOMPARE ( colour_at ( document, 3 ), tokens.stringValue );
	}

	void numbers_literals_and_punctuation_each_get_their_colour ()
	{
		QTextDocument document;

		JsonHighlighter highlighter ( &document );

		const CodeTokenPalette tokens = CodeTokenPalette::for_palette ( palette_with_base ( LIGHT_BASE ) );

		highlighter.set_token_palette ( tokens );

		const QString text = QStringLiteral ( "{ \"a\": 42, \"b\": true, \"c\": null }" );

		set_text ( document, highlighter, text );

		QCOMPARE ( colour_at ( document, text.indexOf ( QStringLiteral ( "42" ) ) ),   tokens.number );
		QCOMPARE ( colour_at ( document, text.indexOf ( QStringLiteral ( "true" ) ) ), tokens.literal );
		QCOMPARE ( colour_at ( document, text.indexOf ( QStringLiteral ( "null" ) ) ), tokens.literal );
		QCOMPARE ( colour_at ( document, 0 ),                                          tokens.punctuation );
	}

	void an_invalid_tail_is_left_uncoloured ()
	{
		QTextDocument document;

		JsonHighlighter highlighter ( &document );

		highlighter.set_token_palette ( CodeTokenPalette::for_palette ( palette_with_base ( LIGHT_BASE ) ) );

		// A half-typed string -- the state the buffer is in constantly. The valid prefix still colours; the damage
		// simply is not painted, rather than being painted as an error.

		const QString text = QStringLiteral ( "{ \"a\": 1, \"b\": \"unterminated" );

		set_text ( document, highlighter, text );

		QCOMPARE ( colour_at ( document, text.indexOf ( QStringLiteral ( "1" ) ) ),
		           highlighter.token_palette ().number );

		QVERIFY2 ( !colour_at ( document, text.length () - 1 ).isValid (),
		           "the damaged tail must be left unpainted rather than painted as an error" );
	}

	//=================================================================================================================
	// CodeTokenPalette.
	//=================================================================================================================

	void light_and_dark_are_decided_by_the_base_colour ()
	{
		QVERIFY ( !CodeTokenPalette::is_dark_surface ( palette_with_base ( LIGHT_BASE ) ) );
		QVERIFY (  CodeTokenPalette::is_dark_surface ( palette_with_base ( DARK_BASE ) ) );

		// Two genuinely different families, not one set reused -- a dark-theme member name would be unreadable on white.

		QVERIFY ( CodeTokenPalette::for_palette ( palette_with_base ( LIGHT_BASE ) ).memberName
		       != CodeTokenPalette::for_palette ( palette_with_base ( DARK_BASE ) ).memberName );
	}

	void chrome_moves_away_from_the_surface_under_both_themes ()
	{
		// The STYLE-04 failure, stated as a test: a fixed offset that lifts a dark ground clear of its content runs off
		// the end of the scale on a light one, and the symptom is chrome nobody can see on exactly one theme.

		const CodeTokenPalette onLight = CodeTokenPalette::for_palette ( palette_with_base ( LIGHT_BASE ) );
		const CodeTokenPalette onDark  = CodeTokenPalette::for_palette ( palette_with_base ( DARK_BASE ) );

		QVERIFY2 ( onLight.gutterBackground.lightness () < LIGHT_BASE.lightness (),
		           "on a light surface the gutter must go darker" );

		QVERIFY2 ( onDark.gutterBackground.lightness () > DARK_BASE.lightness (),
		           "on a dark surface the gutter must go lighter" );

		QVERIFY2 ( onLight.currentLineBackground.lightness () < LIGHT_BASE.lightness (),
		           "on a light surface the current-line bar must go darker" );

		QVERIFY2 ( onDark.currentLineBackground.lightness () > DARK_BASE.lightness (),
		           "on a dark surface the current-line bar must go lighter" );
	}

	void the_caret_line_number_stands_clear_of_the_others ()
	{
		for ( const QColor& base : { LIGHT_BASE, DARK_BASE } )
		{
			const CodeTokenPalette tokens = CodeTokenPalette::for_palette ( palette_with_base ( base ) );

			// Distance from the gutter surface, not raw lightness -- on a dark theme "clearer" means lighter and on a
			// light one it means darker, so comparing the numbers directly would assert the opposite on one of them.

			const int ordinary = qAbs ( tokens.gutterText.lightness        () - base.lightness () );
			const int current  = qAbs ( tokens.gutterCurrentText.lightness () - base.lightness () );

			QVERIFY2 ( current > ordinary, "the caret's line number must be lifted out of the column" );
		}
	}
};

QTEST_MAIN ( TestJsonHighlighter )

#include "tst_json_highlighter.moc"

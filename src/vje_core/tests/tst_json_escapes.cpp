//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for json_escapes -- the single escape table, and SET-03's three display modes built on it.
//
//   THE CASE THAT MATTERS MOST is the round trip: string_commit_value must be the exact inverse of string_edit_text
//   under the same mode. That one property is what makes "the mode is the notation" true, and with it the answer to
//   what a paste means, what a loaded file means, and whether editing a value can quietly change it. It is asserted
//   over a value carrying every escape at once rather than one at a time, because the failure mode worth catching is a
//   table that is right per-character and wrong about ordering or about the backslash.
//
//   The second is that the table is genuinely SHARED. JsonParser::decode_string and JsonSerializer::write_string both
//   delegate here now, so a case pins escape() against the exact spelling the serializer used to emit -- if the two
//   ever part company, a document round trip changes and this is the cheapest place to notice.
//
//   Headless: pure QString transformations, no QObject anywhere.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/json_escapes.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestJsonEscapes : public QObject
{
	Q_OBJECT

private slots:

	void every_two_character_escape_survives_both_ways ();
	void the_solidus_decodes_but_is_never_produced ();
	void a_unicode_escape_decodes_but_is_never_produced ();
	void a_control_character_below_the_table_becomes_a_unicode_escape ();
	void a_raw_control_character_decodes_as_itself ();
	void a_malformed_escape_fails_and_reports_where ();
	void an_unfinished_escape_fails ();
	void flatten_removes_every_control_character ();
	void the_three_modes_show_what_they_say ();
	void flattened_edits_in_escaped_notation ();
	void commit_is_the_exact_inverse_of_edit ();
	void decoded_mode_takes_a_backslash_literally ();

private:

	// One value carrying every escape at once, plus a plain word either side -- the shape that catches an ordering or
	// backslash mistake a per-character case would not.

	static QString every_escape_value ();
};

QString TestJsonEscapes::every_escape_value ()
{
	return QStringLiteral ( "a\"b\\c/d\bе\ff\ng\rh\ti" ).replace ( QStringLiteral ( "е" ), QStringLiteral ( "e" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestJsonEscapes::every_two_character_escape_survives_both_ways ()
{
	const QString value = every_escape_value ();

	// The exact spelling JsonSerializer emitted before the table moved out of it. A change here changes every saved
	// document, so it is pinned literally rather than by round trip alone.

	QCOMPARE ( json_escapes::escape ( value ), QStringLiteral ( "a\\\"b\\\\c/d\\be\\ff\\ng\\rh\\ti" ) );

	QString decoded;

	QVERIFY ( json_escapes::decode ( json_escapes::escape ( value ), decoded ) );
	QCOMPARE ( decoded, value );
}

void TestJsonEscapes::the_solidus_decodes_but_is_never_produced ()
{
	// RFC 8259 permits "\/" and requires nothing. Escaping it would make every file path in a document unreadable, so
	// it is accepted on the way in and never written on the way out.

	QString decoded;

	QVERIFY ( json_escapes::decode ( QStringLiteral ( "C:\\/Users" ), decoded ) );
	QCOMPARE ( decoded, QStringLiteral ( "C:/Users" ) );

	QCOMPARE ( json_escapes::escape ( QStringLiteral ( "C:/Users" ) ), QStringLiteral ( "C:/Users" ) );
}

void TestJsonEscapes::a_unicode_escape_decodes_but_is_never_produced ()
{
	// SET-03's scope: a file may contain \uXXXX and must load, but an accented character always DISPLAYS as itself.
	// Rendering it as \u00e9 would make any non-English document unreadable and diverge from what a save writes.

	QString decoded;

	QVERIFY ( json_escapes::decode ( QStringLiteral ( "caf\\u00e9" ), decoded ) );
	QCOMPARE ( decoded, QStringLiteral ( "café" ) );

	QCOMPARE ( json_escapes::escape ( QStringLiteral ( "café" ) ), QStringLiteral ( "café" ) );

	// A surrogate pair falls out as two UTF-16 code units forming one code point.

	QVERIFY ( json_escapes::decode ( QStringLiteral ( "\\ud83d\\ude00" ), decoded ) );
	QCOMPARE ( decoded, QStringLiteral ( "\U0001F600" ) );
}

void TestJsonEscapes::a_control_character_below_the_table_becomes_a_unicode_escape ()
{
	// A vertical tab has no two-character escape, and RFC 8259 forbids it raw inside a string -- so it is the one kind
	// of character escape() has to produce a \u form for.
	//
	// Built rather than written as "a\x0bb": a C++ hex escape is GREEDY, so that literal is one character U+0BB
	// followed by nothing, not a vertical tab followed by 'b'. The first draft of this case asserted against exactly
	// that and failed while the code was right.

	const QString withVerticalTab = QStringLiteral ( "a" ) + QChar ( 0x000B ) + QStringLiteral ( "b" );

	QCOMPARE ( json_escapes::escape ( withVerticalTab ), QStringLiteral ( "a\\u000bb" ) );

	QString decoded;

	QVERIFY ( json_escapes::decode ( QStringLiteral ( "a\\u000bb" ), decoded ) );
	QCOMPARE ( decoded, withVerticalTab );
}

void TestJsonEscapes::a_raw_control_character_decodes_as_itself ()
{
	// The deliberate tolerance (json_escapes.hpp): a real tab pasted into an escaped field means a tab. It is not an
	// error, and it reappears escaped once committed -- which is what makes paste and typing land on the same value.

	QString decoded;

	QVERIFY ( json_escapes::decode ( QStringLiteral ( "a\tb" ), decoded ) );
	QCOMPARE ( decoded, QStringLiteral ( "a\tb" ) );

	QCOMPARE ( json_escapes::escape ( decoded ), QStringLiteral ( "a\\tb" ) );
}

void TestJsonEscapes::a_malformed_escape_fails_and_reports_where ()
{
	QString decoded;
	int     errorOffset = -1;

	QVERIFY ( !json_escapes::decode ( QStringLiteral ( "ab\\qcd" ), decoded, &errorOffset ) );
	QCOMPARE ( errorOffset, 2 );

	// Everything decoded up to the fault is kept, so a caller can say how far it got.

	QCOMPARE ( decoded, QStringLiteral ( "ab" ) );

	// A \u form with too few digits, and one with a non-hex digit, are the same kind of failure.

	QVERIFY ( !json_escapes::decode ( QStringLiteral ( "\\u12" ),   decoded ) );
	QVERIFY ( !json_escapes::decode ( QStringLiteral ( "\\u12g4" ), decoded ) );
}

void TestJsonEscapes::an_unfinished_escape_fails ()
{
	// A trailing lone backslash is unfinished rather than wrong -- the user is mid-escape -- but it still cannot be
	// committed, which is why the validator answers Intermediate for both shapes and the commit refuses both.

	QString decoded;
	int     errorOffset = -1;

	QVERIFY ( !json_escapes::decode ( QStringLiteral ( "abc\\" ), decoded, &errorOffset ) );
	QCOMPARE ( errorOffset, 3 );
}

void TestJsonEscapes::flatten_removes_every_control_character ()
{
	QCOMPARE
	(
		json_escapes::flatten ( QStringLiteral ( "A tabbed field\t123\nLine 2" ) ),
		QStringLiteral ( "A tabbed field123Line 2" )
	);

	// Only below U+0020: an ordinary space is not a control character and must survive.

	QCOMPARE ( json_escapes::flatten ( QStringLiteral ( "a b" ) ), QStringLiteral ( "a b" ) );
}

void TestJsonEscapes::the_three_modes_show_what_they_say ()
{
	const QString value = QStringLiteral ( "A tabbed field\t123\nLine 2" );

	QCOMPARE ( string_display_text ( value, StringDisplay::Escaped ),   QStringLiteral ( "A tabbed field\\t123\\nLine 2" ) );
	QCOMPARE ( string_display_text ( value, StringDisplay::Decoded ),   value );
	QCOMPARE ( string_display_text ( value, StringDisplay::Flattened ), QStringLiteral ( "A tabbed field123Line 2" ) );

	// The backslash consequence, stated in spec section 2.10 and pinned here because it is the one that surprises: a
	// single backslash shows as two in escaped notation, and that is correct rather than a doubling bug.

	QCOMPARE ( string_display_text ( QStringLiteral ( "C:\\Users" ), StringDisplay::Escaped ), QStringLiteral ( "C:\\\\Users" ) );
}

void TestJsonEscapes::flattened_edits_in_escaped_notation ()
{
	const QString value = QStringLiteral ( "a\tb" );

	// Flattened removes characters, so it cannot be its own edit notation without every edit destroying them. Its
	// editor therefore opens in Escaped -- which is why the cell visibly changes as the editor opens.

	QCOMPARE ( string_display_text ( value, StringDisplay::Flattened ), QStringLiteral ( "ab" ) );
	QCOMPARE ( string_edit_text    ( value, StringDisplay::Flattened ), QStringLiteral ( "a\\tb" ) );

	QVERIFY (  mode_edits_in_escaped_notation ( StringDisplay::Flattened ) );
	QVERIFY (  mode_edits_in_escaped_notation ( StringDisplay::Escaped ) );
	QVERIFY ( !mode_edits_in_escaped_notation ( StringDisplay::Decoded ) );
}

void TestJsonEscapes::commit_is_the_exact_inverse_of_edit ()
{
	// THE property. "The mode is the notation" is true exactly when opening an editor and committing it unchanged
	// leaves the value alone -- in every mode, including the one whose display is lossy.

	const QStringList values =
	{
		every_escape_value (),
		QStringLiteral ( "A tabbed field\t123\nLine 2" ),
		QStringLiteral ( "C:\\Users\\Rohin" ),
		QStringLiteral ( "plain" ),
		QStringLiteral ( "café \U0001F600" ),
		QString ()
	};

	const QList<StringDisplay> modes = { StringDisplay::Escaped, StringDisplay::Decoded, StringDisplay::Flattened };

	for ( const QString& value : values )
	{
		for ( const StringDisplay mode : modes )
		{
			QString committed;

			QVERIFY2
			(
				string_commit_value ( string_edit_text ( value, mode ), mode, committed ),
				qPrintable ( QStringLiteral ( "edit text did not decode: %1" ).arg ( value ) )
			);

			QVERIFY2
			(
				committed == value,
				qPrintable ( QStringLiteral ( "round trip changed \"%1\" to \"%2\"" ).arg ( value, committed ) )
			);
		}
	}
}

void TestJsonEscapes::decoded_mode_takes_a_backslash_literally ()
{
	// The other half of "the mode is the notation": in raw text a backslash is a backslash, so \t is two characters and
	// not a tab. Anything else would make a literal backslash-t impossible to enter.

	QString committed;

	QVERIFY ( string_commit_value ( QStringLiteral ( "a\\tb" ), StringDisplay::Decoded, committed ) );
	QCOMPARE ( committed, QStringLiteral ( "a\\tb" ) );
	QCOMPARE ( committed.length (), 4 );

	// And in escaped notation the same four characters are three, the middle one a tab.

	QVERIFY ( string_commit_value ( QStringLiteral ( "a\\tb" ), StringDisplay::Escaped, committed ) );
	QCOMPARE ( committed, QStringLiteral ( "a\tb" ) );
	QCOMPARE ( committed.length (), 3 );

	// Decoded mode never refuses: there is no notation to get wrong.

	QVERIFY ( string_commit_value ( QStringLiteral ( "trailing \\" ), StringDisplay::Decoded, committed ) );
	QVERIFY ( !string_commit_value ( QStringLiteral ( "trailing \\" ), StringDisplay::Escaped, committed ) );
}

QTEST_APPLESS_MAIN ( TestJsonEscapes )

#include "tst_json_escapes.moc"

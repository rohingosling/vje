//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for ClipboardService -- the dual-format node / cell clipboard (EDIT-06, EDITOR-11). Two halves:
//
//     - the PURE encode / decode over QMimeData, which is where the format contract lives: the private exact-JSON
//       format wins over plain text (so an in-app paste keeps a number's type and its raw token, FILE-10), the source
//       key of a node copy round-trips, and external plain text resolves as JSON-or-string. These need no live
//       clipboard.
//     - one LIVE round-trip through the offscreen QClipboard, so the wiring from copy_* to value() is checked end to
//       end and not just the codec.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/ClipboardService.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QtTest/QtTest>

#include <memory>

using namespace vje;

class TestClipboardService : public QObject
{
	Q_OBJECT

private slots:

	void cell_plain_text_matches_the_tree_copy_form ();
	void the_private_format_wins_over_plain_text ();
	void a_node_copy_round_trips_its_source_key ();
	void external_plain_text_resolves_as_json_or_string ();
	void empty_content_is_reported_as_such ();
	void a_live_cell_copy_round_trips ();
	void has_content_tracks_external_clipboard_changes ();
};

//---------------------------------------------------------------------------------------------------------------------
// Pure encode / decode
//---------------------------------------------------------------------------------------------------------------------

void TestClipboardService::cell_plain_text_matches_the_tree_copy_form ()
{
	QCOMPARE ( ClipboardService::cell_plain_text ( *JsonNode::make_string  ( QStringLiteral ( "hi" ) ) ), QStringLiteral ( "hi" ) );
	QCOMPARE ( ClipboardService::cell_plain_text ( *JsonNode::make_number  ( QStringLiteral ( "1.50" ) ) ), QStringLiteral ( "1.50" ) );
	QCOMPARE ( ClipboardService::cell_plain_text ( *JsonNode::make_boolean ( true ) ), QStringLiteral ( "true" ) );
	QCOMPARE ( ClipboardService::cell_plain_text ( *JsonNode::make_null    () ), QStringLiteral ( "null" ) );
}

void TestClipboardService::the_private_format_wins_over_plain_text ()
{
	// A number cell copy carries "1.50" as exact JSON in the private format and as plain text. Decoding must prefer the
	// private format so the pasted value is a NUMBER with its raw token, not the string "1.50".

	QMimeData data;

	ClipboardService::fill_cell_mime ( &data, *JsonNode::make_number ( QStringLiteral ( "1.50" ) ) );

	const std::unique_ptr<JsonNode> value = ClipboardService::value_from_mime ( &data );

	QVERIFY  ( value != nullptr );
	QCOMPARE ( static_cast<int> ( value->kind () ), static_cast<int> ( JsonKind::Number ) );
	QCOMPARE ( value->number_token (), QStringLiteral ( "1.50" ) );

	QVERIFY ( ClipboardService::mime_has_content ( &data ) );
}

void TestClipboardService::a_node_copy_round_trips_its_source_key ()
{
	QMimeData data;

	ClipboardService::fill_node_mime ( &data, *JsonNode::make_string ( QStringLiteral ( "alex" ) ), QStringLiteral ( "name" ) );

	QCOMPARE ( ClipboardService::source_key_from_mime ( &data ), QStringLiteral ( "name" ) );

	// A copy with no source key (an array element, or the root) carries none.

	QMimeData keyless;

	ClipboardService::fill_node_mime ( &keyless, *JsonNode::make_string ( QStringLiteral ( "x" ) ), QString () );

	QVERIFY ( ClipboardService::source_key_from_mime ( &keyless ).isEmpty () );
}

void TestClipboardService::external_plain_text_resolves_as_json_or_string ()
{
	// Text from an external editor: valid JSON parses to its type; anything else is a plain string (EDITOR-11).

	QMimeData jsonText;
	jsonText.setText ( QStringLiteral ( "true" ) );

	const std::unique_ptr<JsonNode> parsed = ClipboardService::value_from_mime ( &jsonText );

	QVERIFY  ( parsed != nullptr );
	QCOMPARE ( static_cast<int> ( parsed->kind () ), static_cast<int> ( JsonKind::Boolean ) );

	QMimeData words;
	words.setText ( QStringLiteral ( "just words" ) );

	const std::unique_ptr<JsonNode> asString = ClipboardService::value_from_mime ( &words );

	QVERIFY  ( asString != nullptr );
	QCOMPARE ( static_cast<int> ( asString->kind () ), static_cast<int> ( JsonKind::String ) );
	QCOMPARE ( asString->string_value (), QStringLiteral ( "just words" ) );
}

void TestClipboardService::empty_content_is_reported_as_such ()
{
	QMimeData empty;

	QVERIFY ( !ClipboardService::mime_has_content ( &empty ) );
	QVERIFY ( ClipboardService::value_from_mime ( &empty ) == nullptr );
	QVERIFY ( ClipboardService::value_from_mime ( nullptr ) == nullptr );
}

//---------------------------------------------------------------------------------------------------------------------
// Live round-trip through the offscreen clipboard
//---------------------------------------------------------------------------------------------------------------------

void TestClipboardService::a_live_cell_copy_round_trips ()
{
	ClipboardService service ( QApplication::clipboard () );

	// The clipboard may carry residue from the environment before the copy; nothing is asserted about that state.

	service.copy_cell ( *JsonNode::make_number ( QStringLiteral ( "7" ) ) );

	QVERIFY ( service.has_content () );

	const std::unique_ptr<JsonNode> value = service.value ();

	QVERIFY  ( value != nullptr );
	QCOMPARE ( static_cast<int> ( value->kind () ), static_cast<int> ( JsonKind::Number ) );
	QCOMPARE ( value->number_token (), QStringLiteral ( "7" ) );

	// A node copy carries its source key through the live clipboard too.

	service.copy_node ( *JsonNode::make_string ( QStringLiteral ( "v" ) ), QStringLiteral ( "email" ) );

	QCOMPARE ( service.source_key (), QStringLiteral ( "email" ) );
}

void TestClipboardService::has_content_tracks_external_clipboard_changes ()
{
	// has_content answers from a cached flag refreshed on dataChanged -- one OS read per clipboard change rather than
	// one per query, because the enablement recompute asks on every keyboard-focus change (NFR-03). The cache must
	// track writes the service did not make itself: another application's copy, and another application's clear.

	ClipboardService service ( QApplication::clipboard () );

	QApplication::clipboard ()->setText ( QStringLiteral ( "outside text" ) );

	QVERIFY ( service.has_content () );

	QApplication::clipboard ()->setMimeData ( new QMimeData () );   // An external clear.

	QVERIFY ( !service.has_content () );
}

QTEST_MAIN ( TestClipboardService )

#include "tst_clipboard_service.moc"

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
	void external_text_is_only_text_from_another_application ();
	void empty_content_is_reported_as_such ();
	void a_live_cell_copy_round_trips ();
	void set_plain_text_leaves_no_private_format ();
	void has_content_tracks_external_clipboard_changes ();

	// EDIT-14's ordered list (Phase 15f).

	void a_node_list_round_trips_its_nodes_keys_and_order ();
	void a_list_of_one_is_written_as_a_single_node_copy ();
	void an_array_element_and_an_empty_keyed_member_stay_distinguishable ();
	void a_node_list_writes_plain_text_an_external_editor_can_use ();
	void only_the_private_list_format_answers_as_a_list ();
	void a_list_copy_reports_content_for_the_paste_enablement ();
	void a_live_node_list_copy_round_trips ();

	// EDITOR-18's table row / column (Phase 15h).

	void a_table_selection_round_trips_its_shape_and_values ();
	void an_absent_cell_stays_distinguishable_from_a_null_one ();
	void a_row_writes_its_json_and_a_column_writes_a_value_per_line ();
	void only_the_private_table_format_answers_as_a_table ();
	void a_table_copy_reports_content_for_the_paste_enablement ();
	void a_table_selection_says_whether_its_cells_were_members ();
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

void TestClipboardService::external_text_is_only_text_from_another_application ()
{
	// EDITOR-24 reads EXTERNAL text as a block of cells, so the one thing it must never be handed is VJE's own plain
	// text -- a rendering written for other applications, which read back as a block would turn a copied cell holding
	// line breaks into several cells. Every private format disqualifies, each asserted, and the text alone qualifies.

	QMimeData external;
	external.setText ( QStringLiteral ( "a\tb\nc\td" ) );

	QCOMPARE ( ClipboardService::external_text_from_mime ( &external ), QStringLiteral ( "a\tb\nc\td" ) );

	for ( const QString& format : { clipboard_mime::VJE_JSON, clipboard_mime::VJE_JSON_LIST, clipboard_mime::VJE_JSON_TABLE } )
	{
		QMimeData ours;
		ours.setText ( QStringLiteral ( "a\nb" ) );
		ours.setData ( format, QByteArrayLiteral ( "[]" ) );

		QVERIFY2 ( ClipboardService::external_text_from_mime ( &ours ).isEmpty (), qPrintable ( format ) );
	}

	QVERIFY ( ClipboardService::external_text_from_mime ( nullptr ).isEmpty () );
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

void TestClipboardService::set_plain_text_leaves_no_private_format ()
{
	// THE set_* HALF OF THE NAMING RULE, pinned on the SERVICE that owns it rather than at any one caller.
	//
	// It had been asserted twice, through FindController and through JsonPathView, and not at all for the third
	// caller -- FormGridController's key copy, which is the one where the consequence lands in the user's DOCUMENT: a
	// key copied with the private format still attached makes the next Ctrl+V insert a whole subtree where the user
	// believed they had copied a member's name. Undoable, and only once noticed.
	//
	// Asserting it here covers every caller present and future, which is what stops the rule being re-proved by hand
	// each time a command that copies a NAME is added -- and 15f is about to add a list form to the private format.

	ClipboardService service ( QApplication::clipboard () );

	// A node copy FIRST, so both private formats are provably present and the case cannot pass against a clipboard
	// that never had them (the vacuous-assertion trap, lesson D20/D21).

	service.copy_node ( *JsonNode::make_string ( QStringLiteral ( "alex" ) ), QStringLiteral ( "name" ) );

	const QMimeData* const afterNodeCopy = QApplication::clipboard ()->mimeData ();

	QVERIFY ( afterNodeCopy != nullptr );
	QVERIFY ( afterNodeCopy->hasFormat ( clipboard_mime::VJE_JSON ) );
	QVERIFY ( afterNodeCopy->hasFormat ( clipboard_mime::VJE_JSON_KEY ) );

	service.set_plain_text ( QStringLiteral ( "/people/0/name" ) );

	const QMimeData* const afterTextCopy = QApplication::clipboard ()->mimeData ();

	QVERIFY  ( afterTextCopy != nullptr );
	QVERIFY2 ( !afterTextCopy->hasFormat ( clipboard_mime::VJE_JSON ),
	           "a name copy must not leave the private node format behind for the next paste to find" );
	QVERIFY2 ( !afterTextCopy->hasFormat ( clipboard_mime::VJE_JSON_KEY ),
	           "a name copy must not leave a previous copy's source key behind either" );

	QCOMPARE ( QApplication::clipboard ()->text (), QStringLiteral ( "/people/0/name" ) );

	// THE USER-VISIBLE CONSEQUENCE, asserted as what a paste would actually insert rather than as a property of the
	// MIME table. What comes back is the TEXT as a string, not the node that was copied a moment ago.

	const std::unique_ptr<JsonNode> pasted = service.value ();

	QVERIFY  ( pasted != nullptr );
	QCOMPARE ( static_cast<int> ( pasted->kind () ), static_cast<int> ( JsonKind::String ) );
	QCOMPARE ( pasted->string_value (), QStringLiteral ( "/people/0/name" ) );

	// AND THE EMPTY STRING CLEARS, which is the case most likely to be "optimized" away. The ROOT's pointer is the
	// empty string (RFC 6901), so copying the root is a success that legitimately leaves the clipboard empty -- and an
	// implementation that returned early on an empty argument would leave the previous node copy sitting there, so
	// copying the root's pointer would paste a node.

	service.copy_node ( *JsonNode::make_string ( QStringLiteral ( "alex" ) ), QStringLiteral ( "name" ) );

	QVERIFY ( QApplication::clipboard ()->mimeData ()->hasFormat ( clipboard_mime::VJE_JSON ) );

	service.set_plain_text ( QString () );

	QVERIFY2 ( !QApplication::clipboard ()->mimeData ()->hasFormat ( clipboard_mime::VJE_JSON ),
	           "copying the root's empty pointer must still clear what was there" );

	QVERIFY ( QApplication::clipboard ()->text ().isEmpty () );
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

//---------------------------------------------------------------------------------------------------------------------
// EDIT-14 -- the ordered node list
//---------------------------------------------------------------------------------------------------------------------

void TestClipboardService::a_node_list_round_trips_its_nodes_keys_and_order ()
{
	// The whole contract in one case: what went in comes out, in the same order, with each node's own key beside it,
	// and with a number keeping its RAW TOKEN (FILE-10) exactly as the single format preserves it.

	auto first  = JsonNode::make_number ( QStringLiteral ( "1.50" ) );
	auto second = JsonNode::make_string ( QStringLiteral ( "two" ) );
	auto third  = JsonNode::make_object ();

	third->append_member ( QStringLiteral ( "inner" ), JsonNode::make_boolean ( true ) );

	QMimeData data;

	ClipboardService::fill_node_list_mime
	(
		&data,
		{
			{ first.get  (), QStringLiteral ( "alpha" ) },
			{ second.get (), QStringLiteral ( "beta" ) },
			{ third.get  (), QStringLiteral ( "gamma" ) }
		}
	);

	const std::vector<ClipboardNodeValue> entries = ClipboardService::value_list_from_mime ( &data );

	QCOMPARE ( entries.size (), std::size_t ( 3 ) );

	QCOMPARE ( entries [ 0 ].key, QStringLiteral ( "alpha" ) );
	QCOMPARE ( entries [ 1 ].key, QStringLiteral ( "beta" ) );
	QCOMPARE ( entries [ 2 ].key, QStringLiteral ( "gamma" ) );

	QCOMPARE ( entries [ 0 ].node->kind (), JsonKind::Number );
	QCOMPARE ( entries [ 0 ].node->number_token (), QStringLiteral ( "1.50" ) );
	QCOMPARE ( entries [ 1 ].node->string_value (), QStringLiteral ( "two" ) );

	QVERIFY ( entries [ 2 ].node->equals ( *third ) );
}

void TestClipboardService::a_list_of_one_is_written_as_a_single_node_copy ()
{
	// EDIT-14's promise that nothing about the pre-15f clipboard changes for the case that already worked. A one-node
	// copy must be indistinguishable from copy_node's output, or an external paste, a cell paste and an in-app node
	// paste would all start behaving differently the day multiple selection shipped.

	auto node = JsonNode::make_string ( QStringLiteral ( "only" ) );

	QMimeData listForm;
	QMimeData singleForm;

	ClipboardService::fill_node_list_mime ( &listForm,   { { node.get (), QStringLiteral ( "key" ) } } );
	ClipboardService::fill_node_mime      ( &singleForm, *node, QStringLiteral ( "key" ) );

	QVERIFY ( !listForm.hasFormat ( clipboard_mime::VJE_JSON_LIST ) );
	QVERIFY (  listForm.hasFormat ( clipboard_mime::VJE_JSON ) );

	QCOMPARE ( listForm.data ( clipboard_mime::VJE_JSON ),     singleForm.data ( clipboard_mime::VJE_JSON ) );
	QCOMPARE ( listForm.data ( clipboard_mime::VJE_JSON_KEY ), singleForm.data ( clipboard_mime::VJE_JSON_KEY ) );
	QCOMPARE ( listForm.text (),                               singleForm.text () );

	// And it does NOT read back as a list, which is what sends a one-node paste down the single-node route.

	QVERIFY ( ClipboardService::value_list_from_mime ( &listForm ).empty () );
}

void TestClipboardService::an_array_element_and_an_empty_keyed_member_stay_distinguishable ()
{
	// The reason the format writes "key" as ABSENT rather than empty for an array element: an object member may
	// legitimately be keyed with the empty string, and a format that spelled the two the same could not paste that
	// member back under its own name.

	auto element = JsonNode::make_number ( QStringLiteral ( "1" ) );
	auto member  = JsonNode::make_number ( QStringLiteral ( "2" ) );

	QMimeData data;

	ClipboardService::fill_node_list_mime
	(
		&data,
		{
			{ element.get (), QString () },                          // Null -- an array element has no key.
			{ member.get  (), QString ( QLatin1String ( "" ) ) }     // Empty but not null -- a member keyed "".
		}
	);

	const std::vector<ClipboardNodeValue> entries = ClipboardService::value_list_from_mime ( &data );

	QCOMPARE ( entries.size (), std::size_t ( 2 ) );

	QVERIFY2 ( entries [ 0 ].key.isNull (),  "an array element must come back with no key at all" );
	QVERIFY2 ( !entries [ 1 ].key.isNull (), "a member keyed with the empty string must keep its key" );
	QVERIFY  ( entries [ 1 ].key.isEmpty () );
}

void TestClipboardService::a_node_list_writes_plain_text_an_external_editor_can_use ()
{
	// EDIT-14's plain-text rule: each node as it appears in its parent, comma-separated, one per line, so the result
	// drops straight between an object's braces. Asserted as the exact text, because "readable JSON" is the half of
	// this a user checks by pasting it somewhere.

	auto first  = JsonNode::make_number ( QStringLiteral ( "1.50" ) );
	auto second = JsonNode::make_string ( QStringLiteral ( "two" ) );

	QMimeData objectMembers;

	ClipboardService::fill_node_list_mime
	(
		&objectMembers,
		{ { first.get (), QStringLiteral ( "alpha" ) }, { second.get (), QStringLiteral ( "beta" ) } }
	);

	QCOMPARE ( objectMembers.text (), QStringLiteral ( "\"alpha\": 1.50,\n\"beta\": \"two\"" ) );

	// Array elements have no names, so they come out as bare values -- a fragment for an array literal instead.

	QMimeData arrayElements;

	ClipboardService::fill_node_list_mime
	(
		&arrayElements,
		{ { first.get (), QString () }, { second.get (), QString () } }
	);

	QCOMPARE ( arrayElements.text (), QStringLiteral ( "1.50,\n\"two\"" ) );
}

void TestClipboardService::only_the_private_list_format_answers_as_a_list ()
{
	// Plain text is deliberately NOT a fallback for value_list. An external editor's copied JSON array is ONE value,
	// and reading it as several nodes would turn a paste of an array into a paste of its elements -- a different edit
	// from a gesture that looked the same.

	QMimeData external;

	external.setText ( QStringLiteral ( "[1, 2, 3]" ) );

	QVERIFY ( ClipboardService::value_list_from_mime ( &external ).empty () );
	QVERIFY ( ClipboardService::value_from_mime ( &external ) != nullptr );

	QVERIFY ( ClipboardService::value_list_from_mime ( nullptr ).empty () );

	// And a list payload that is not the shape this service writes is refused rather than half-read.

	QMimeData malformed;

	malformed.setData ( clipboard_mime::VJE_JSON_LIST, QByteArrayLiteral ( "{ not a list" ) );

	QVERIFY ( ClipboardService::value_list_from_mime ( &malformed ).empty () );
}

void TestClipboardService::a_list_copy_reports_content_for_the_paste_enablement ()
{
	auto first  = JsonNode::make_number ( QStringLiteral ( "1" ) );
	auto second = JsonNode::make_number ( QStringLiteral ( "2" ) );

	QMimeData data;

	ClipboardService::fill_node_list_mime ( &data, { { first.get (), QString () }, { second.get (), QString () } } );

	QVERIFY ( ClipboardService::mime_has_content ( &data ) );
}

void TestClipboardService::a_live_node_list_copy_round_trips ()
{
	// Through the real QClipboard, which is what the application uses -- the pure cases above never leave process
	// memory, and a MIME type the platform declined to carry would pass every one of them.

	ClipboardService service ( QApplication::clipboard () );

	auto first  = JsonNode::make_string ( QStringLiteral ( "a" ) );
	auto second = JsonNode::make_string ( QStringLiteral ( "b" ) );

	service.copy_nodes ( { { first.get (), QStringLiteral ( "one" ) }, { second.get (), QStringLiteral ( "two" ) } } );

	const std::vector<ClipboardNodeValue> entries = service.value_list ();

	QCOMPARE ( entries.size (), std::size_t ( 2 ) );
	QCOMPARE ( entries [ 0 ].key, QStringLiteral ( "one" ) );
	QCOMPARE ( entries [ 1 ].node->string_value (), QStringLiteral ( "b" ) );
	QVERIFY  ( service.has_content () );
}

//=====================================================================================================================
// EDITOR-18's table row / column (Phase 15h)
//=====================================================================================================================

void TestClipboardService::a_table_selection_round_trips_its_shape_and_values ()
{
	// The shape travels WITH the values rather than being implied by one format per shape, so a paste of the wrong one
	// can say what is on the clipboard instead of merely failing to recognize it.

	const std::unique_ptr<JsonNode> first  = JsonNode::make_number ( QStringLiteral ( "1.50" ) );
	const std::unique_ptr<JsonNode> second = JsonNode::make_string ( QStringLiteral ( "b" ) );

	QMimeData data;

	ClipboardService::fill_table_mime
	(
		&data,
		TableSelectionShape::Column,
		{ { first.get (), QStringLiteral ( "n" ) }, { second.get (), QStringLiteral ( "n" ) } },
		QStringLiteral ( "1.50\nb" )
	);

	const TableSelectionValue restored = ClipboardService::table_selection_from_mime ( &data );

	QCOMPARE ( restored.shape, TableSelectionShape::Column );
	QCOMPARE ( restored.cells.size (), std::size_t ( 2 ) );

	// FILE-10: the raw number token survives, for the reason it does in the single and list formats -- the subtree
	// nests inside a payload the serializer writes and the parser reads, rather than going through a bespoke codec.

	QCOMPARE ( restored.cells [ 0 ].value->number_token (), QStringLiteral ( "1.50" ) );
	QCOMPARE ( restored.cells [ 1 ].value->string_value (), QStringLiteral ( "b" ) );

	// The KEY travels too, which is what lets a paste that GROWS its target name the value it appends.

	QCOMPARE ( restored.cells [ 0 ].key, QStringLiteral ( "n" ) );

	// And the other shape is not the same value read differently.

	QMimeData rowData;

	ClipboardService::fill_table_mime ( &rowData, TableSelectionShape::Row, { { first.get (), QString () } }, QString () );

	QCOMPARE ( ClipboardService::table_selection_from_mime ( &rowData ).shape, TableSelectionShape::Row );
}

void TestClipboardService::an_absent_cell_stays_distinguishable_from_a_null_one ()
{
	// EDITOR-18 treats the two differently on paste -- an absent source leaves its target alone, a null one overwrites
	// it -- so a format in which they were the same value could not carry a ragged row.

	const std::unique_ptr<JsonNode> nullValue = JsonNode::make_null ();

	QMimeData data;

	ClipboardService::fill_table_mime
	(
		&data,
		TableSelectionShape::Row,
		{ { nullptr, QStringLiteral ( "a" ) }, { nullValue.get (), QStringLiteral ( "b" ) } },
		QString ()
	);

	const TableSelectionValue restored = ClipboardService::table_selection_from_mime ( &data );

	QCOMPARE ( restored.cells.size (), std::size_t ( 2 ) );
	QVERIFY  ( restored.cells [ 0 ].value == nullptr );
	QVERIFY  ( restored.cells [ 1 ].value != nullptr );
	QCOMPARE ( restored.cells [ 1 ].value->kind (), JsonKind::Null );

	// An absent VALUE still carries its key: it is a cell the element lacks, not a column that does not exist.

	QCOMPARE ( restored.cells [ 0 ].key, QStringLiteral ( "a" ) );
}

void TestClipboardService::a_row_writes_its_json_and_a_column_writes_a_value_per_line ()
{
	// The plain text is the caller's, so what this pins is that it reaches the clipboard untouched -- which is what
	// lets a row hand an external editor its JSON and a column hand a spreadsheet a column.

	const std::unique_ptr<JsonNode> value = JsonNode::make_string ( QStringLiteral ( "x" ) );

	QMimeData rowData;

	ClipboardService::fill_table_mime
	(
		&rowData, TableSelectionShape::Row, { { value.get (), QStringLiteral ( "n" ) } }, QStringLiteral ( R"({"n":"x"})" )
	);

	QCOMPARE ( rowData.text (), QStringLiteral ( R"({"n":"x"})" ) );

	QMimeData columnData;

	ClipboardService::fill_table_mime
	(
		&columnData,
		TableSelectionShape::Column,
		{ { value.get (), QStringLiteral ( "n" ) }, { nullptr, QStringLiteral ( "n" ) } },
		QStringLiteral ( "x\n" )
	);

	// The trailing empty line is the ABSENT cell -- section 2.12's CSV rule, keeping "does not have that member"
	// distinguishable from "has it, and it is null".

	QCOMPARE ( columnData.text (), QStringLiteral ( "x\n" ) );
}

void TestClipboardService::only_the_private_table_format_answers_as_a_table ()
{
	// value_list's rule, for the same reason: an external editor's copied text is one value, and reading it as a row
	// or a column would turn one paste into a different edit from a gesture that looked the same.

	QMimeData external;

	external.setText ( QStringLiteral ( "[1, 2, 3]" ) );

	QVERIFY ( ClipboardService::table_selection_from_mime ( &external ).is_empty () );
	QVERIFY ( ClipboardService::value_from_mime ( &external ) != nullptr );

	QVERIFY ( ClipboardService::table_selection_from_mime ( nullptr ).is_empty () );

	// A payload that is not the shape this service writes is refused rather than half-read.

	QMimeData malformed;

	malformed.setData ( clipboard_mime::VJE_JSON_TABLE, QByteArrayLiteral ( "{ not a table" ) );

	QVERIFY ( ClipboardService::table_selection_from_mime ( &malformed ).is_empty () );

	// Including one that parses but carries neither member.

	QMimeData wrongShape;

	wrongShape.setData ( clipboard_mime::VJE_JSON_TABLE, QByteArrayLiteral ( R"({"shape":"column"})" ) );

	QVERIFY ( ClipboardService::table_selection_from_mime ( &wrongShape ).is_empty () );
}

void TestClipboardService::a_table_copy_reports_content_for_the_paste_enablement ()
{
	// has_content drives the Paste enablement, and it reads mime_has_content -- so a format added without being named
	// there leaves Paste greyed out after a copy that visibly worked.
	//
	// THE PLAIN TEXT IS DELIBERATELY EMPTY, and that is what makes this a claim about the format rather than about the
	// text beside it. Every ordinary copy also sets text, which mime_has_content already answers yes to -- so with a
	// non-empty string here the case passes whether or not the format is named at all. An empty one is reachable: a
	// column every element of which lacks the member writes nothing but blank lines.

	const std::unique_ptr<JsonNode> value = JsonNode::make_number ( QStringLiteral ( "7" ) );

	ClipboardService service ( QApplication::clipboard () );

	service.copy_table_selection ( TableSelectionShape::Row, { { value.get (), QStringLiteral ( "n" ) } }, QString () );

	QVERIFY ( service.has_content () );

	const TableSelectionValue restored = service.table_selection ();

	QCOMPARE ( restored.shape, TableSelectionShape::Row );
	QCOMPARE ( restored.cells.size (), std::size_t ( 1 ) );
	QCOMPARE ( restored.cells [ 0 ].value->number_token (), QStringLiteral ( "7" ) );
}


void TestClipboardService::a_table_selection_says_whether_its_cells_were_members ()
{
	// EDITOR-18's INSERT (2026-09-23) needs to know. Every cell carries a name -- a single-value column is named after
	// its array (section 2.12) -- so a row of VALUES and a row from a one-column array of OBJECTS look the same by
	// their names alone, and inserted into an array of values one goes in bare and the other as an object.
	//
	// The flag is written only when false, so a payload without it reads as keyed: the first half pins that, the
	// second that false survives the round trip.

	const std::unique_ptr<JsonNode> value = JsonNode::make_string ( QStringLiteral ( "b" ) );

	QMimeData keyed;

	ClipboardService::fill_table_mime ( &keyed, TableSelectionShape::Row, { { value.get (), QStringLiteral ( "v" ) } }, QString () );

	QVERIFY ( ClipboardService::table_selection_from_mime ( &keyed ).keyed );

	QMimeData unkeyed;

	ClipboardService::fill_table_mime
	(
		&unkeyed, TableSelectionShape::Row, { { value.get (), QStringLiteral ( "items" ) } }, QString (), false
	);

	QVERIFY ( !ClipboardService::table_selection_from_mime ( &unkeyed ).keyed );
}

QTEST_MAIN ( TestClipboardService )

#include "tst_clipboard_service.moc"

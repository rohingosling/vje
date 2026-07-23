//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for CodeView and CodeEditor -- the raw-JSON editing view (EDITOR-07 /
//   EDITOR-09).
//
//   The claims, each of which is a decision the implementation could plausibly have made differently:
//
//     - WHAT IT SHOWS IS WHAT SAVE WRITES (EDITOR-07 / FILE-03). Asserted against JsonFormatter under the same profile
//       reader, so the two cannot drift apart without this failing.
//     - VALIDATION GATES THE COMMIT (EDITOR-07). Invalid text refuses to commit, reports its line and column, and
//       leaves the document untouched -- the "never writes a stale model" half.
//     - A COMMIT IS ONE UNDO STEP, and it does NOT reformat the user's text out from under them.
//     - DUPLICATE KEYS ARE REJECTED ON COMMIT AND TOLERATED ON LOAD (VAL-02). The asymmetry is deliberate and would
//       look like a bug either way round.
//     - LEAVING AUTO-COMMITS WHEN VALID AND ABORTS WHEN NOT (EDITOR-09). view_deactivating()'s two answers, which are
//       the reason that seam exists on IEditorView at all.
//     - ESC DISCARDS, restoring the committed text.
//     - TREE NAVIGATION IS NON-DESTRUCTIVE (EDITOR-09). present() during an uncommitted edit neither commits nor
//       discards nor re-renders.
//     - THE TWO REVEAL CHANNELS ARE SEPARATE (EDITOR-04). A selection scrolls and leaves the caret alone; the
//       activation gesture moves it. This is the caret/scroll split that cost version 1.0 a phase.
//     - TAB IS THE VIEW'S (EDITOR-07 / NAV-04), and it indents by the document format profile.
//
//   Runs offscreen. Note what that costs: the offscreen platform grants keyboard focus to nothing, so
//   nothing here asserts where the FOCUS ends up -- only what the text, the caret, the scroll offset and the document
//   do. The focus half stays with manual smoke.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"
#include "views/CodeEditor.hpp"
#include "views/CodeView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"id": 1001,
		"name": "Alex Rivera",
		"profile": { "city": "Cape Town", "country": "ZA" },
		"roles": [ "admin", "editor" ],
		"projects":
		[
			{ "name": "JSON Editor",    "status": "in-progress" },
			{ "name": "Data Migration", "status": "completed"   }
		]
	})";
}

class TestCodeView : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir>  settingsDirectory;
	std::unique_ptr<SettingsStore>  settings;
	std::unique_ptr<JsonDocument>   document;
	std::unique_ptr<UndoController> undo;
	std::unique_ptr<CodeView>       view;

	// Typing, as the user does it -- through the widget, so the textChanged / validation path is the one under test
	// rather than a member being set directly.

	void set_editor_text ( const QString& text )
	{
		view->editor ()->setPlainText ( text );

		// The validation pass is debounced off a timer; the tests want its answer now rather than in 150 ms.

		QTest::qWait ( config::code::VALIDATION_DEBOUNCE + 60 );
	}

	void build_fixture ()
	{
		// Strict reverse dependency order on teardown: the view watches the document and the undo
		// controller writes to it, so both must be gone before the document is.

		view.reset ();
		undo.reset ();
		document.reset ();
		settings.reset ();
		settingsDirectory.reset ();

		settingsDirectory = std::make_unique<QTemporaryDir> ();

		settings = std::make_unique<SettingsStore>
		(
			settingsDirectory->filePath ( QStringLiteral ( "settings.json" ) )
		);

		document = std::make_unique<JsonDocument> ();

		ParseResult parsed = JsonParser::parse ( QString::fromUtf8 ( SAMPLE_DOCUMENT ) );

		QVERIFY ( parsed.ok );

		document->set_root ( std::move ( parsed.root ) );

		undo = std::make_unique<UndoController> ( document.get () );
		view = std::make_unique<CodeView> ( document.get (), undo.get (), settings.get (), nullptr );

		view->resize ( 600, 400 );
	}

private slots:

	void init ()
	{
		build_fixture ();
	}

	void cleanup ()
	{
		view.reset ();
		undo.reset ();
		document.reset ();
		settings.reset ();
		settingsDirectory.reset ();
	}

	//=================================================================================================================
	// What it shows.
	//=================================================================================================================

	void the_text_is_byte_for_byte_the_saved_format ()
	{
		// EDITOR-07 / FILE-03's claim, taken through the SAME profile reader the save path uses -- so if the two ever
		// read a different set of keys, this fails rather than a user discovering it in a diff.

		QCOMPARE
		(
			view->editor ()->toPlainText (),
			JsonFormatter::format ( *document->root (), document_format_profile ( settings.get () ) )
		);
	}

	void the_format_profile_setting_reformats_the_text ()
	{
		settings->set_string ( settings_keys::CODE_INDENT_KIND, settings_values::INDENT_TABS );

		QCOMPARE
		(
			view->editor ()->toPlainText (),
			JsonFormatter::format ( *document->root (), document_format_profile ( settings.get () ) )
		);

		QVERIFY ( view->editor ()->toPlainText ().contains ( QLatin1Char ( '\t' ) ) );
	}

	void a_fresh_view_has_no_uncommitted_edit ()
	{
		// The refresh writes the whole buffer, which fires textChanged. Mistaking that for the user typing would mark
		// every freshly loaded document as dirty.

		QVERIFY ( !view->has_uncommitted_edit () );
		QVERIFY ( view->is_text_valid () );
	}

	//=================================================================================================================
	// Validation gates the commit (EDITOR-07).
	//=================================================================================================================

	void invalid_text_is_reported_with_its_position ()
	{
		set_editor_text ( QStringLiteral ( "{ \"a\": }" ) );

		QVERIFY ( !view->is_text_valid () );
		QVERIFY ( !view->validation_message ().isEmpty () );

		// The position is what makes the message actionable rather than merely discouraging.

		QVERIFY ( view->validation_message ().contains ( QStringLiteral ( "Line" ) ) );
		QVERIFY ( view->validation_message ().contains ( QStringLiteral ( "column" ) ) );
	}

	void an_invalid_edit_cannot_reach_the_document ()
	{
		set_editor_text ( QStringLiteral ( "{ \"a\": }" ) );

		QVERIFY ( !view->commit_now () );

		// Untouched: the "never writes a stale model" half of EDITOR-07.

		QVERIFY ( document->root ()->has_member ( QStringLiteral ( "name" ) ) );
		QVERIFY ( !undo->can_undo () );
	}

	void a_commit_that_would_introduce_a_duplicate_key_is_refused ()
	{
		// VAL-02's asymmetry: a LOADED file keeps its duplicates, but an edit that creates one is rejected, because a
		// pointer names the first match and the second would be unreachable.

		set_editor_text ( QStringLiteral ( "{ \"a\": 1, \"a\": 2 }" ) );

		QVERIFY ( !view->commit_now () );
		QVERIFY ( !undo->can_undo () );
	}

	void an_empty_buffer_is_not_an_error ()
	{
		// What the user has for one keystroke after select-all-and-type. Flagging it puts an error on screen for a
		// document nobody has finished describing.

		set_editor_text ( QString () );

		QVERIFY ( view->is_text_valid () );
		QVERIFY ( view->validation_message ().isEmpty () );
	}

	//=================================================================================================================
	// A valid commit.
	//=================================================================================================================

	void a_valid_edit_commits_as_one_undo_step ()
	{
		set_editor_text ( QStringLiteral ( "{ \"a\": 1, \"b\": [ 2, 3 ] }" ) );

		QVERIFY ( view->commit_now () );

		QCOMPARE ( document->root ()->member_count (), 2 );
		QVERIFY  ( document->root ()->has_member ( QStringLiteral ( "b" ) ) );

		QVERIFY ( undo->can_undo () );

		undo->undo ();

		// ONE step back is the whole document, not one member of it.

		QVERIFY ( document->root ()->has_member ( QStringLiteral ( "name" ) ) );
		QVERIFY ( !undo->can_undo () );
	}

	void a_commit_does_not_reformat_the_text_under_the_caret ()
	{
		// The user's own spacing survives Ctrl+S. Regenerating the buffer from the document here would reformat their
		// text the instant they saved it, which is the most jarring thing an editor can do.

		const QString typed = QStringLiteral ( "{\"a\":1,\"b\":2}" );

		set_editor_text ( typed );

		QVERIFY ( view->commit_now () );

		QCOMPARE ( view->editor ()->toPlainText (), typed );
		QVERIFY  ( !view->has_uncommitted_edit () );
	}

	void committing_unchanged_text_is_a_success_and_a_no_op ()
	{
		QVERIFY ( view->commit_now () );
		QVERIFY ( !undo->can_undo () );
	}

	//=================================================================================================================
	// EDITOR-09 -- leaving, and discarding.
	//=================================================================================================================

	void leaving_with_a_valid_edit_auto_commits ()
	{
		set_editor_text ( QStringLiteral ( "{ \"a\": 1 }" ) );

		QVERIFY2 ( view->view_deactivating (), "a valid edit must not stand in the way of leaving" );

		QCOMPARE ( document->root ()->member_count (), 1 );
		QVERIFY  ( !view->has_uncommitted_edit () );
	}

	void leaving_with_nothing_uncommitted_is_silent ()
	{
		QVERIFY ( view->view_deactivating () );
		QVERIFY ( !undo->can_undo () );
	}

	void esc_discards_and_restores_the_committed_text ()
	{
		const QString committed = view->editor ()->toPlainText ();

		set_editor_text ( QStringLiteral ( "{ \"a\": 1 }" ) );

		QVERIFY ( view->has_uncommitted_edit () );

		view->discard_edit ();

		QCOMPARE ( view->editor ()->toPlainText (), committed );
		QVERIFY  ( !view->has_uncommitted_edit () );
		QVERIFY  ( !undo->can_undo () );
	}

	void tree_navigation_during_an_edit_neither_commits_nor_discards ()
	{
		// EDITOR-09's non-destructive rule. A whole-document commit here would rebuild the tree under the user, and a
		// discard would throw away work they can see in front of them.

		const QString edited = QStringLiteral ( "{\n  \"a\": 1,\n  \"b\": 2\n}" );

		set_editor_text ( edited );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/b" ) ), SelectionOrigin::Tree );

		QCOMPARE ( view->editor ()->toPlainText (), edited );
		QVERIFY  ( view->has_uncommitted_edit () );
		QVERIFY  ( !undo->can_undo () );
		QVERIFY  ( document->root ()->has_member ( QStringLiteral ( "name" ) ) );
	}

	void an_edit_elsewhere_does_not_overwrite_an_uncommitted_edit ()
	{
		const QString edited = QStringLiteral ( "{ \"a\": 1 }" );

		set_editor_text ( edited );

		undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), QStringLiteral ( "Sam Patel" ) );

		QCOMPARE ( view->editor ()->toPlainText (), edited );
	}

	void an_edit_elsewhere_refreshes_a_clean_view ()
	{
		// EDITOR-08, the other half: with nothing to defend, a change made in the Form View shows here.

		undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), QStringLiteral ( "Sam Patel" ) );

		QVERIFY ( view->editor ()->toPlainText ().contains ( QStringLiteral ( "Sam Patel" ) ) );
		QVERIFY ( !view->has_uncommitted_edit () );
	}

	//=================================================================================================================
	// The two reveal channels (EDITOR-04) -- the caret / scroll split.
	//=================================================================================================================

	void a_selection_scrolls_without_moving_the_caret ()
	{
		const int caretBefore = view->editor ()->caret_line ();

		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects/1/status" ) ), SelectionOrigin::Tree );

		QCOMPARE ( view->editor ()->caret_line (), caretBefore );
	}

	void the_activation_gesture_moves_the_caret_to_the_node ()
	{
		const JsonPointer target = JsonPointer::parse ( QStringLiteral ( "/profile/country" ) );

		view->present ( target, SelectionOrigin::Tree );

		view->activate_editing ();

		const QString caretLineText = view->editor ()->document ()
			->findBlockByNumber ( view->editor ()->caret_line () - 1 ).text ();

		QVERIFY2 ( caretLineText.contains ( QStringLiteral ( "country" ) ),
		           qPrintable ( QStringLiteral ( "the caret landed on: %1" ).arg ( caretLineText ) ) );
	}

	void the_caret_lands_past_the_indentation ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );

		view->activate_editing ();

		QVERIFY2 ( view->editor ()->textCursor ().positionInBlock () > 0,
		           "the caret belongs where the content is, not in the left margin" );
	}

	void revealing_a_node_the_edited_text_no_longer_holds_does_nothing ()
	{
		set_editor_text ( QStringLiteral ( "{ \"a\": 1 }" ) );

		const int caretBefore  = view->editor ()->caret_line ();
		const int scrollBefore = view->editor ()->verticalScrollBar ()->value ();

		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ), SelectionOrigin::Tree );

		// Staying put is the right answer -- scrolling somewhere arbitrary would be worse than not moving.

		QCOMPARE ( view->editor ()->caret_line (), caretBefore );
		QCOMPARE ( view->editor ()->verticalScrollBar ()->value (), scrollBefore );
	}

	//=================================================================================================================
	// The keyboard (EDITOR-07 / NAV-04).
	//=================================================================================================================

	void the_view_claims_the_tab_key ()
	{
		// Which is what takes it out of the NAV-04 pane cycle while it holds the keyboard -- an editor that loses the
		// caret to another pane on Tab cannot be typed in.

		QVERIFY ( view->claims_tab_key () );
	}

	void tab_inserts_the_profiles_indent_rather_than_a_tab_character ()
	{
		set_editor_text ( QStringLiteral ( "{}" ) );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( 0 );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Tab );

		// Two spaces -- the SET-07 default -- and specifically NOT "\t", which is the whole point of tying the key to
		// the document format profile.

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "  {}" ) );
	}

	void tab_follows_a_changed_indent_size ()
	{
		settings->set_int ( settings_keys::CODE_INDENT_SIZE, 4 );

		set_editor_text ( QStringLiteral ( "{}" ) );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( 0 );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Tab );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "    {}" ) );
	}

	void shift_tab_outdents_the_caret_line ()
	{
		set_editor_text ( QStringLiteral ( "  \"a\": 1" ) );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( 5 );   // Somewhere in the middle of the line, not at its start.

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Backtab );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "\"a\": 1" ) );
	}

	void tab_indents_a_multi_line_selection_as_a_block ()
	{
		set_editor_text ( QStringLiteral ( "\"a\": 1,\n\"b\": 2" ) );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( 0 );
		cursor.setPosition ( view->editor ()->toPlainText ().length (), QTextCursor::KeepAnchor );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Tab );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "  \"a\": 1,\n  \"b\": 2" ) );

		// Re-selected, so a second Tab indents the same lines again rather than replacing them with an indent.

		QTest::keyClick ( view->editor (), Qt::Key_Tab );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "    \"a\": 1,\n    \"b\": 2" ) );
	}

	void a_block_indent_undoes_in_one_step ()
	{
		set_editor_text ( QStringLiteral ( "\"a\": 1,\n\"b\": 2" ) );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( 0 );
		cursor.setPosition ( view->editor ()->toPlainText ().length (), QTextCursor::KeepAnchor );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Tab );

		view->editor ()->undo ();

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "\"a\": 1,\n\"b\": 2" ) );
	}

	//=================================================================================================================
	// Syntax highlighting (SET-07).
	//=================================================================================================================

	void highlighting_can_be_switched_off_and_on ()
	{
		// Read from the block's LAYOUT formats, which is where a QSyntaxHighlighter publishes its colouring. The
		// fragments' own char formats are the underlying text format and carry the palette's ordinary text colour
		// whether anything is highlighted or not -- so a check written against them answers "coloured" always, and
		// agrees with a highlighter that has been switched off.

		const auto is_coloured = [ this ] ()
		{
			const QTextBlock block = view->editor ()->document ()->findBlockByNumber ( 1 );

			return block.isValid () && ( block.layout () != nullptr ) && !block.layout ()->formats ().isEmpty ();
		};

		QVERIFY2 ( is_coloured (), "SET-07 defaults syntax highlighting to on" );

		settings->set_bool ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, false );

		QVERIFY ( !is_coloured () );

		settings->set_bool ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, true );

		QVERIFY ( is_coloured () );
	}

	//=================================================================================================================
	// The provider.
	//=================================================================================================================

	void the_provider_is_last_in_the_strip ()
	{
		const CodeViewProvider provider ( document.get (), undo.get (), settings.get (), nullptr );

		QCOMPARE ( provider.view_id (), QStringLiteral ( "code" ) );
		QCOMPARE ( provider.display_order (), 2 );

		QVERIFY ( provider.can_present ( document->root () ) );
		QVERIFY ( !provider.can_present ( nullptr ) );

		QVERIFY ( !provider.icon_name ().isEmpty () );
	}
};

QTEST_MAIN ( TestCodeView )

#include "tst_code_view.moc"

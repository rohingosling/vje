//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include "services/SelectionService.hpp"
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

	std::unique_ptr<QTemporaryDir>    settingsDirectory;
	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<CodeView>         view;

	// The 1-based line of the first rendered line containing needle, or 0. Cases derive their line numbers this way so
	// they state WHICH ELEMENT they clicked on rather than a coordinate the format profile decides.

	int line_containing ( const QString& needle ) const
	{
		const QStringList lines = view->editor ()->toPlainText ().split ( QLatin1Char ( '\n' ) );

		for ( int index = 0; index < lines.size (); ++index )
		{
			if ( lines [ index ].contains ( needle ) )
			{
				return index + 1;
			}
		}

		return 0;
	}

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
		selection.reset ();
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
		selection = std::make_unique<SelectionService> ();

		view = std::make_unique<CodeView> ( document.get (), undo.get (), settings.get (), nullptr, selection.get () );

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
		selection.reset ();
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

	//=================================================================================================================
	// Printing (FILE-12).
	//=================================================================================================================

	void what_is_printed_is_the_buffer_including_an_uncommitted_edit ()
	{
		const PrintContent committed = view->print_content ( 0 );

		QCOMPARE ( committed.kind, PrintContent::Kind::Preformatted );
		QCOMPARE ( committed.text, view->editor ()->toPlainText () );

		// This view always shows the WHOLE document, so it names no node -- a subject here would say something untrue
		// about what is on the page.

		QVERIFY ( committed.subject.isEmpty () );

		// And an UNCOMMITTED edit prints as it stands. That is what "print the active view's rendering" means, and it
		// is why printing does not run the EDITOR-09 departure gate: a read-only command that stopped to ask keep /
		// discard would both surprise the user and print something other than what is on their screen.

		view->editor ()->setPlainText ( QStringLiteral ( "{ \"typed\": true }" ) );

		QVERIFY ( view->has_uncommitted_edit () );

		QCOMPARE ( view->print_content ( 0 ).text, QStringLiteral ( "{ \"typed\": true }" ) );
	}

	void an_empty_buffer_prints_nothing ()
	{
		view->editor ()->setPlainText ( QString () );

		QVERIFY ( view->print_content ( 0 ).is_empty () );
	}

	void a_printed_line_too_wide_for_the_page_continues_under_its_own_indent ()
	{
		// What a JSON line MEANS is read off its indentation, so a continuation starting at column 0 would read as a
		// sibling at the document root. The page would break it there; this view breaks it under the line's own indent
		// first, so the page never has to.

		constexpr int PAGE_COLUMNS = 60;

		view->editor ()->setPlainText
		(
			QStringLiteral ( "{\n        \"description\": \"" )
			+ QStringLiteral ( "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima" )
			+ QStringLiteral ( "\"\n}" )
		);

		const QStringList lines = view->print_content ( PAGE_COLUMNS ).text.split ( QLatin1Char ( '\n' ) );

		QVERIFY2 ( lines.size () > 3, qPrintable ( lines.join ( QLatin1Char ( '\n' ) ) ) );

		int continuations = 0;

		for ( const QString& line : lines )
		{
			QVERIFY2
			(
				line.length () <= PAGE_COLUMNS,
				qPrintable ( QStringLiteral ( "%1 characters: %2" ).arg ( line.length () ).arg ( line ) )
			);

			// The wrapped pieces of the long line are the ones carrying its eight-space indent and no quoted key.

			if ( line.startsWith ( QStringLiteral ( "        " ) ) && !line.contains ( QStringLiteral ( "\"description\"" ) ) )
			{
				++continuations;

				QVERIFY2 ( !line.startsWith ( QStringLiteral ( "         " ) ), qPrintable ( line ) );
			}
		}

		QVERIFY2 ( continuations > 0, qPrintable ( lines.join ( QLatin1Char ( '\n' ) ) ) );

		// The lines that already fit are untouched -- the braces are still at the margin where the format put them.

		QCOMPARE ( lines.first (), QStringLiteral ( "{" ) );
		QCOMPARE ( lines.last (),  QStringLiteral ( "}" ) );
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
	// The double click names a node (EDITOR-07, the reverse of the reveal channel).
	//=================================================================================================================

	void a_double_click_reports_the_caret_line ()
	{
		// The editor's whole half of the gesture: translate a double click into a LINE. The claim is deliberately
		// "the line the caret landed on" rather than "the line I aimed at" -- where the click lands is Qt's business,
		// and a runner with no fonts has no reliable geometry to aim with (a lesson from CI).

		CodeEditor* const editor = view->editor ();

		editor->move_caret_to_line ( 3 );

		QSignalSpy doubleClicked ( editor, &CodeEditor::line_double_clicked );

		QTest::mouseDClick ( editor->viewport (), Qt::LeftButton, Qt::NoModifier, editor->cursorRect ().center () );

		QCOMPARE ( doubleClicked.count (), 1 );
		QCOMPARE ( doubleClicked.first ().first ().toInt (), editor->caret_line () );

		// And the gesture keeps its ordinary meaning: the word under the cursor is selected.

		QVERIFY ( editor->textCursor ().hasSelection () );
	}

	void a_double_click_selects_the_node_that_line_belongs_to ()
	{
		// The line is DERIVED from the rendered text rather than written in, so this survives a change to the format
		// profile's defaults -- which decide how many lines anything occupies.

		const int cityLine = line_containing ( QStringLiteral ( "\"city\"" ) );

		QVERIFY ( cityLine > 0 );

		emit view->editor ()->line_double_clicked ( cityLine );

		QVERIFY  ( selection->has_selection () );
		QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/profile/city" ) );

		// The origin carries reveal intent, because the node may be inside a collapsed branch and finding it in the
		// tree is the entire point of the gesture.

		QCOMPARE ( static_cast<int> ( selection->origin () ), static_cast<int> ( SelectionOrigin::CodeCaret ) );
		QVERIFY  ( reveals_selection ( selection->origin () ) );
	}

	void a_double_click_on_a_containers_brace_selects_the_container ()
	{
		// The closing brace of /profile -- the line after its last member under the Allman default. A start-line-only
		// index would answer with the last member instead, which is the wrong node by one level.

		const int closingLine = line_containing ( QStringLiteral ( "\"country\"" ) ) + 1;

		QVERIFY ( view->editor ()->toPlainText ().split ( QLatin1Char ( '\n' ) ).value ( closingLine - 1 ).contains ( QLatin1Char ( '}' ) ) );

		emit view->editor ()->line_double_clicked ( closingLine );

		QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/profile" ) );
	}

	void a_double_click_on_a_node_the_document_does_not_have_selects_nothing ()
	{
		// An uncommitted edit: the index is built from the TEXT, so it knows about a member the document has never
		// heard of. Publishing that would name a selection nothing can resolve.

		set_editor_text ( QStringLiteral ( "{\n  \"id\": 1001,\n  \"invented\": 7\n}" ) );

		QVERIFY ( view->has_uncommitted_edit () );

		emit view->editor ()->line_double_clicked ( 3 );

		QVERIFY ( !selection->has_selection () );

		// The line that IS in the document still answers, so the refusal is about the node and not about the edit.

		emit view->editor ()->line_double_clicked ( 2 );

		QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/id" ) );
	}

	void a_double_click_below_a_syntax_error_selects_nothing ()
	{
		set_editor_text ( QStringLiteral ( "{\n  \"id\": ,,,\n  \"name\": \"Alex Rivera\"\n}" ) );

		emit view->editor ()->line_double_clicked ( 3 );

		QVERIFY ( !selection->has_selection () );
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
		const CodeViewProvider provider ( document.get (), undo.get (), settings.get (), nullptr, selection.get () );

		QCOMPARE ( provider.view_id (), QStringLiteral ( "code" ) );
		QCOMPARE ( provider.display_order (), 2 );

		QVERIFY ( provider.can_present ( document->root () ) );
		QVERIFY ( !provider.can_present ( nullptr ) );

		QVERIFY ( !provider.icon_name ().isEmpty () );
	}

	// NFR-05. Named at the USE site rather than inside CodeEditor, because that class is also the Import XML dialog's
	// read-only preview -- one name baked into the class would be wrong in one of the two places.

	void the_editor_carries_an_accessible_name ()
	{
		QPlainTextEdit* const editor = view->findChild<QPlainTextEdit*> ();

		QVERIFY ( editor != nullptr );
		QVERIFY2 ( !editor->accessibleName ().isEmpty (), "The Code View editor has no accessible name" );
	}

	// NAV-06's shortcut pair, measured rather than assumed (lesson D13). Alt+Shift+Left / Right became a window command
	// for the splitter, which is only safe if the text editor underneath does nothing with it -- and the Code View is
	// the most demanding consumer, being a full QPlainTextEdit. If a future Qt binds the combination, this fails and
	// says so, instead of the splitter quietly stealing a key the editor had started using.

	void alt_shift_arrows_are_free_in_the_code_editor ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QPlainTextEdit* const editor = view->findChild<QPlainTextEdit*> ();

		QVERIFY ( editor != nullptr );

		QTextCursor cursor = editor->textCursor ();

		cursor.setPosition ( 4 );

		editor->setTextCursor ( cursor );

		const int before = editor->textCursor ().position ();

		QTest::keyClick ( editor, Qt::Key_Left,  Qt::AltModifier | Qt::ShiftModifier );
		QTest::keyClick ( editor, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier );

		QCOMPARE ( editor->textCursor ().position (), before );

		QVERIFY2 ( !editor->textCursor ().hasSelection (), "Alt+Shift+arrow selected text in the code editor" );
	}
};

QTEST_MAIN ( TestCodeView )

#include "tst_code_view.moc"

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
//     - A FORMAT CHANGE WAITS FOR AN UNCOMMITTED EDIT AND IS PAID WHEN IT ENDS (SET-07, Phase 15k.2) -- by a commit
//       as much as by a discard, once and no more -- and the caret stays on its node across a re-format.
//     - DUPLICATE KEYS ARE REJECTED ON COMMIT AND TOLERATED ON LOAD (VAL-02). The asymmetry is deliberate and would
//       look like a bug either way round.
//     - LEAVING AUTO-COMMITS WHEN VALID AND ABORTS WHEN NOT (EDITOR-09). view_deactivating()'s two answers, which are
//       the reason that seam exists on IEditorView at all.
//     - ESC DISCARDS, restoring the committed text.
//     - TREE NAVIGATION IS NON-DESTRUCTIVE (EDITOR-09). present() during an uncommitted edit neither commits nor
//       discards nor re-renders.
//     - THE TWO REVEAL CHANNELS ARE SEPARATE (EDITOR-04). A selection scrolls and leaves the caret alone; the
//       activation gesture moves it. This is the caret/scroll split that cost version 1.0 a phase.
//     - TAB IS THE VIEW'S (EDITOR-07 / NAV-04), and it indents by the document format profile. ENTER keeps the indent
//       by the same profile, and neither writes to a read-only editor. BACKSPACE in the indentation goes back to where
//       the brackets say, in one undo step, and leaves a selection and a caret after text to Qt.
//     - FOLDING (EDITOR-23): a fold hides its interior and keeps both brackets; a gutter click and the fold keys toggle
//       it; a line asked for -- by the tree, by the activation gesture, by the caret landing in it -- is a line shown;
//       the scroll counts visible lines; folds stay on their NODES across a refresh and across typing, and open when
//       the user edits inside them; a load opens them all; and printing prints what is shown.
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
#include "services/settings_profiles.hpp"
#include "services/SelectionService.hpp"
#include "views/CodeEditor.hpp"
#include "views/CodeView.hpp"
#include "views/code_folding.hpp"
#include "views/json_text_index.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>
#include <QSignalSpy>

#include <QImage>
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

	// A node's fold region in the rendered text -- so a case names the NODE it folds, and the format profile decides
	// the line numbers. Region derivation itself is tst_code_folding's; here it only locates the brackets.

	FoldRegion region_of ( const QString& pointerText ) const
	{
		for ( const FoldRegion& region : fold_regions ( build_pointer_span_index ( view->editor ()->toPlainText () ) ) )
		{
			if ( region.pointer == pointerText )
			{
				return region;
			}
		}

		return FoldRegion ();
	}

	bool folded_between ( const FoldRegion& region ) const
	{
		return ( region.openLine > 0 ) && folded_between ( region.openLine, region.closeLine );
	}

	QStringList folded () const
	{
		QStringList pointers = view->editor ()->folded_pointers ();

		pointers.sort ();

		return pointers;
	}

	// The fold-marker cell of a line, as rendered: the gutter's rightmost square, one line tall.

	QImage marker_cell ( int line ) const
	{
		QWidget* const gutter = view->findChild<LineNumberArea*> ();
		const int      top    = gutter_y_of ( line );
		const int      cell   = view->editor ()->fontMetrics ().height ();
		const QImage   image  = gutter->grab ().toImage ();
		const qreal    ratio  = image.devicePixelRatio ();

		return image.copy
		(
			QRect
			(
				static_cast<int> ( ( gutter->width () - cell ) * ratio ),
				static_cast<int> ( top * ratio ),
				static_cast<int> ( cell * ratio ),
				static_cast<int> ( cell * ratio )
			)
		);
	}

	static int ink_in ( const QImage& cell, QRgb background )
	{
		int ink = 0;

		for ( int y = 0; y < cell.height (); ++y )
		{
			for ( int x = 0; x < cell.width (); ++x )
			{
				if ( cell.pixel ( x, y ) != background )
				{
					++ink;
				}
			}
		}

		return ink;
	}

	void set_document ( const QString& json )
	{
		ParseResult parsed = JsonParser::parse ( json );

		QVERIFY ( parsed.ok );

		document->set_root ( std::move ( parsed.root ) );
	}

	// The text as the document would show under the current profile, and as it would under Allman: the second is what
	// proves a case changed the brace style rather than passing because the two agree.

	QString formatted_now () const
	{
		return JsonFormatter::format ( *document->root (), document_format_profile ( settings.get () ) );
	}

	QString formatted_allman () const
	{
		FormatProfile profile = document_format_profile ( settings.get () );

		profile.braceStyle = BraceStyle::Allman;

		return JsonFormatter::format ( *document->root (), profile );
	}

	void choose_k_and_r ()
	{
		settings->set_string ( settings_keys::CODE_BRACE_STYLE, settings_values::BRACE_STYLE_K_AND_R );
	}

	// Every line strictly between two lines hidden, and both of them shown: what a fold over that pair looks like.

	bool folded_between ( int openLine, int closeLine ) const
	{
		CodeEditor* const editor = view->editor ();

		if ( !editor->is_line_visible ( openLine ) || !editor->is_line_visible ( closeLine ) )
		{
			return false;
		}

		for ( int line = openLine + 1; line < closeLine; ++line )
		{
			if ( editor->is_line_visible ( line ) )
			{
				return false;
			}
		}

		return true;
	}

	bool nothing_hidden () const
	{
		for ( int line = 1; line <= view->editor ()->blockCount (); ++line )
		{
			if ( !view->editor ()->is_line_visible ( line ) )
			{
				return false;
			}
		}

		return true;
	}

	// A y coordinate in the gutter that lands on a line, found by asking the editor rather than by assuming a line
	// height -- a runner's font decides that (a lesson from CI).

	int gutter_y_of ( int line ) const
	{
		for ( int y = 0; y < view->editor ()->viewport ()->height (); ++y )
		{
			if ( view->editor ()->line_at_gutter_y ( y ) == line )
			{
				return y;
			}
		}

		return -1;
	}

	void show_view ()
	{
		view->show ();

		QVERIFY ( QTest::qWaitForWindowExposed ( view.get () ) );
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

	void an_uncommitted_edit_is_reported_as_unsaved_work ();

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
	// A hidden view does not regenerate (NFR-03).
	//=================================================================================================================

	void a_hidden_view_defers_regeneration_until_it_is_shown ()
	{
		// refresh_from_document() re-serializes, re-highlights and re-parses the WHOLE document. This view spends most
		// of its life as a background tab, so doing that for a change nobody can see is work with no reader --
		// measured at 108 ms for a nine-row column paste in a 4,000-element document, against 3.9 ms without this
		// view alive at all.
		//
		// THE EDITOR IS READ THROUGH findChild RATHER THAN THROUGH editor(), deliberately: that accessor PAYS any
		// deferred refresh, so a case reading through it could not tell a deferral from an immediate refresh and
		// would pass against a build with the rule removed (D20).

		CodeEditor* const raw = view->findChild<CodeEditor*> ();

		QVERIFY ( raw != nullptr );

		view->hide ();

		const QString before = raw->toPlainText ();

		QVERIFY ( !before.contains ( QStringLiteral ( "Sam Patel" ) ) );

		undo->replace_subtree
		(
			JsonPointer::parse ( QStringLiteral ( "/name" ) ),
			JsonNode::make_string ( QStringLiteral ( "Sam Patel" ) ),
			QStringLiteral ( "Edit Value" )
		);

		QCOMPARE ( raw->toPlainText (), before );

		// Shown: the debt is paid, and EDITOR-08 still holds -- an edit made elsewhere is visible here without the
		// document being reloaded.

		view->show ();

		QVERIFY ( raw->toPlainText ().contains ( QStringLiteral ( "Sam Patel" ) ) );
	}

	void a_visible_view_still_regenerates_immediately ()
	{
		// The other half, written as the OPPOSITE so neither passes against a build that ignores visibility in either
		// direction: shown, the same edit reaches the text with nothing asked of the view in between.

		view->show ();

		CodeEditor* const raw = view->findChild<CodeEditor*> ();

		QVERIFY ( raw != nullptr );

		undo->replace_subtree
		(
			JsonPointer::parse ( QStringLiteral ( "/name" ) ),
			JsonNode::make_string ( QStringLiteral ( "Sam Patel" ) ),
			QStringLiteral ( "Edit Value" )
		);

		QVERIFY ( raw->toPlainText ().contains ( QStringLiteral ( "Sam Patel" ) ) );
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

	//=================================================================================================================
	// A format change and an uncommitted edit (SET-07, Phase 15k.2). The change regenerates the text, so it waits for
	// the edit -- and the edit's end, whichever way it ends, pays it.
	//=================================================================================================================

	void a_format_change_waits_for_an_uncommitted_edit ()
	{
		const QString typed = QStringLiteral ( "{\"id\":7,\"profile\":{\"city\":\"X\"}}" );

		set_editor_text ( typed );

		choose_k_and_r ();

		QCOMPARE ( view->editor ()->toPlainText (), typed );
	}

	void a_commit_applies_a_format_change_the_edit_held_back ()
	{
		// The defect: the commit kept the text as typed and forgot the change, so the view stayed in the old brace
		// style while File > Save wrote the new one.

		set_editor_text ( QStringLiteral ( "{\"id\":7,\"profile\":{\"city\":\"X\"}}" ) );

		choose_k_and_r ();

		QVERIFY ( view->commit_now () );

		QCOMPARE ( document->root ()->member_count (), 2 );   // The edit landed as well as the format.

		QCOMPARE ( view->editor ()->toPlainText (), formatted_now () );
		QVERIFY  ( view->editor ()->toPlainText () != formatted_allman () );
		QVERIFY  ( !view->has_uncommitted_edit () );
	}

	void a_discard_applies_a_format_change_the_edit_held_back ()
	{
		set_editor_text ( QStringLiteral ( "{\"id\":7}" ) );

		choose_k_and_r ();

		view->discard_edit ();

		QCOMPARE ( view->editor ()->toPlainText (), formatted_now () );
		QVERIFY  ( view->editor ()->toPlainText () != formatted_allman () );
	}

	void an_edit_typed_back_pays_it_on_ctrl_s ()
	{
		// Typed back to the committed text, the edit ended without a commit or a discard -- Ctrl+S with nothing to
		// commit is still where the user expects the promise kept.

		const QString committed = view->editor ()->toPlainText ();

		set_editor_text ( QStringLiteral ( "{\"id\":7}" ) );

		choose_k_and_r ();

		set_editor_text ( committed );

		QVERIFY ( !view->has_uncommitted_edit () );
		QVERIFY ( view->commit_now () );

		QCOMPARE ( view->editor ()->toPlainText (), formatted_now () );
		QVERIFY  ( view->editor ()->toPlainText () != formatted_allman () );
	}

	void an_edit_typed_back_pays_it_on_leaving ()
	{
		const QString committed = view->editor ()->toPlainText ();

		set_editor_text ( QStringLiteral ( "{\"id\":7}" ) );

		choose_k_and_r ();

		set_editor_text ( committed );

		QVERIFY ( view->view_deactivating () );

		QCOMPARE ( view->editor ()->toPlainText (), formatted_now () );
		QVERIFY  ( view->editor ()->toPlainText () != formatted_allman () );
	}

	void a_format_change_paid_once_is_not_paid_again ()
	{
		// Paid by the discard, so the NEXT commit is an ordinary one, and keeps the user's text as typed.

		set_editor_text ( QStringLiteral ( "{\"id\":7}" ) );

		choose_k_and_r ();

		view->discard_edit ();

		const QString typed = QStringLiteral ( "{\"id\":8}" );

		set_editor_text ( typed );

		QVERIFY ( view->commit_now () );

		QCOMPARE ( view->editor ()->toPlainText (), typed );
	}

	void a_format_change_keeps_the_caret_on_its_node ()
	{
		// "b" is on line 5 under Allman and line 4 under K&R, where line 5 is the closing brace -- so a caret kept on
		// its line NUMBER would end up on another node.

		set_document ( QStringLiteral ( "{\"a\":{\"x\":1,\"b\":2}}" ) );

		const int before = view->editor ()->toPlainText ().split ( QLatin1Char ( '\n' ) ).indexOf ( QStringLiteral ( "    \"b\": 2" ) ) + 1;

		QVERIFY ( before > 0 );

		view->editor ()->move_caret_to_line ( before );

		choose_k_and_r ();

		QVERIFY  ( view->editor ()->caret_line () != before );
		QVERIFY2 ( view->editor ()->textCursor ().block ().text ().contains ( QStringLiteral ( "\"b\"" ) ), qPrintable ( view->editor ()->textCursor ().block ().text () ) );
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

	void the_duplicate_key_setting_lets_a_commit_introduce_one ()
	{
		// SET-03a's other half, and the one 15h.4's smoke test checked by hand: with Allow duplicate keys on, the same
		// commit the case above refuses goes through. The Code View reads the store itself rather than having the policy
		// pushed in, so this is the only place that read is exercised -- a build that ignored the setting passes the case
		// above and fails this one.

		settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, true );

		set_editor_text ( QStringLiteral ( "{ \"a\": 1, \"a\": 2 }" ) );

		QVERIFY  ( view->commit_now () );
		QCOMPARE ( document->root ()->member_count (), 2 );
		QVERIFY  ( undo->can_undo () );
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
	// Enter (EDITOR-07, Phase 15k). The rule is tst_code_indentation's; these cases are the widget's half -- that the
	// key reaches it, that the caret lands where the rule says, and that it is one undo step.
	//=================================================================================================================

	void enter_between_a_bracket_pair_opens_it_and_undoes_in_one_step ()
	{
		const QString before = QStringLiteral ( "{\n  \"a\": {}\n}" );

		set_editor_text ( before );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( before.indexOf ( QStringLiteral ( "{}" ) ) + 1 );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Return );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "{\n  \"a\": {\n    \n  }\n}" ) );

		QCOMPARE ( view->editor ()->caret_line (), 3 );
		QCOMPARE ( view->editor ()->textCursor ().positionInBlock (), 4 );

		view->editor ()->undo ();

		QCOMPARE ( view->editor ()->toPlainText (), before );
	}

	void enter_indents_by_the_profile_from_either_enter_key ()
	{
		// Tabs, so what Enter carries is visibly the profile's character. The keypad's Enter is the same key.

		settings->set_string ( settings_keys::CODE_INDENT_KIND, settings_values::INDENT_TABS );

		set_editor_text ( QStringLiteral ( "{\n\t\"a\": [" ) );

		view->editor ()->moveCursor ( QTextCursor::End );

		QTest::keyClick ( view->editor (), Qt::Key_Return );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "{\n\t\"a\": [\n\t\t" ) );

		QTest::keyClick ( view->editor (), Qt::Key_Enter, Qt::KeypadModifier );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "{\n\t\"a\": [\n\n\t\t" ) );
	}

	void a_read_only_editor_takes_no_enter ()
	{
		// The Import XML preview is this widget read-only. The indentation keys write through a QTextCursor, which the
		// widget's read-only flag does not stop -- so the keys have to stop themselves.

		CodeEditor editor;

		editor.setPlainText ( QStringLiteral ( "{}" ) );
		editor.setReadOnly ( true );

		QTextCursor cursor = editor.textCursor ();

		cursor.setPosition ( 1 );

		editor.setTextCursor ( cursor );

		QTest::keyClick ( &editor, Qt::Key_Return );

		QCOMPARE ( editor.toPlainText (), QStringLiteral ( "{}" ) );
	}

	//=================================================================================================================
	// Backspace (EDITOR-07, Phase 15k.1). The rule is tst_code_indentation's; these cases are the widget's half -- that
	// the key reaches it, with Shift and without, that it is one undo step, and what it leaves to Qt.
	//=================================================================================================================

	void backspace_in_the_indentation_goes_to_the_structure_and_undoes_in_one_step ()
	{
		const QString before = QStringLiteral ( "{\n  \"a\": {\n        \"b\": 1\n  }\n}" );

		set_editor_text ( before );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( before.indexOf ( QStringLiteral ( "\"b\"" ) ) );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Backspace );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "{\n  \"a\": {\n    \"b\": 1\n  }\n}" ) );

		QCOMPARE ( view->editor ()->textCursor ().positionInBlock (), 4 );

		view->editor ()->undo ();

		QCOMPARE ( view->editor ()->toPlainText (), before );
	}

	void shift_backspace_is_the_same_key ()
	{
		// Qt reads Shift+Backspace as Backspace, so a user who has Shift held for the next character gets the same
		// result -- not a single-character delete that only looks the same until the line is deeper.

		const QString before = QStringLiteral ( "[\n      1\n]" );

		set_editor_text ( before );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( before.indexOf ( QLatin1Char ( '1' ) ) );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Backspace, Qt::ShiftModifier );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "[\n  1\n]" ) );
	}

	void backspace_with_a_selection_deletes_the_selection_only ()
	{
		// Two spaces selected inside the indentation, the caret at their end: the selection goes, and no more -- the
		// rule measured from the caret would have taken one character from the anchor instead.

		const QString before    = QStringLiteral ( "[\n      1\n]" );
		const int     lineStart = before.indexOf ( QLatin1Char ( '\n' ) ) + 1;

		set_editor_text ( before );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( lineStart + 1 );
		cursor.setPosition ( lineStart + 3, QTextCursor::KeepAnchor );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Backspace );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "[\n    1\n]" ) );
	}

	void backspace_after_text_removes_one_character ()
	{
		const QString before = QStringLiteral ( "[\n      12\n]" );

		set_editor_text ( before );

		QTextCursor cursor = view->editor ()->textCursor ();

		cursor.setPosition ( before.indexOf ( QLatin1Char ( '2' ) ) + 1 );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Backspace );

		QCOMPARE ( view->editor ()->toPlainText (), QStringLiteral ( "[\n      1\n]" ) );
	}

	//=================================================================================================================
	// Folding (EDITOR-23, Phase 15k).
	//=================================================================================================================

	void a_fold_hides_the_interior_and_keeps_both_brackets ()
	{
		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );

		QVERIFY ( projects.openLine > 0 );
		QVERIFY ( nothing_hidden () );

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		QVERIFY  ( folded_between ( projects ) );
		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects" ) } ) );

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		QVERIFY ( nothing_hidden () );
		QVERIFY ( folded ().isEmpty () );

		// A line that opens nothing is not a fold.

		QVERIFY ( !view->editor ()->toggle_fold ( line_containing ( QStringLiteral ( "\"id\"" ) ) ) );
	}

	void a_gutter_click_on_a_marker_toggles_its_fold ()
	{
		show_view ();

		const FoldRegion profile = region_of ( QStringLiteral ( "/profile" ) );
		QWidget* const   gutter  = view->findChild<LineNumberArea*> ();

		QVERIFY ( gutter != nullptr );

		const int y = gutter_y_of ( profile.openLine );

		QVERIFY ( y >= 0 );

		QTest::mouseClick ( gutter, Qt::LeftButton, Qt::NoModifier, QPoint ( 2, y ) );

		QVERIFY ( folded_between ( profile ) );

		QTest::mouseClick ( gutter, Qt::LeftButton, Qt::NoModifier, QPoint ( 2, y ) );

		QVERIFY ( nothing_hidden () );
	}

	void the_gutter_paints_a_marker_that_turns_when_folded ()
	{
		// Rendered pixels (lesson Q12): a marker is ink in the fold column on a line that opens a region and none on a
		// line that does not, and folding changes what is drawn.

		show_view ();

		const FoldRegion profile   = region_of ( QStringLiteral ( "/profile" ) );
		const int        plainLine = line_containing ( QStringLiteral ( "\"id\"" ) );

		QWidget* const gutter     = view->findChild<LineNumberArea*> ();
		const QImage   image      = gutter->grab ().toImage ();
		const QRgb     background = image.pixel ( 0, static_cast<int> ( gutter_y_of ( plainLine ) * image.devicePixelRatio () ) );

		const QImage open  = marker_cell ( profile.openLine );
		const QImage plain = marker_cell ( plainLine );

		QVERIFY2 ( ink_in ( open, background ) > 0, "no marker on a line that opens a region" );
		QCOMPARE ( ink_in ( plain, background ), 0 );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );

		const QImage closed = marker_cell ( profile.openLine );

		QVERIFY ( ink_in ( closed, background ) > 0 );
		QVERIFY2 ( closed != open, "the marker looks the same folded and open" );
	}

	void the_marker_column_is_only_where_folding_is_on ()
	{
		// The Import XML preview reuses the editor with folding off, and grows no empty column for it.

		CodeEditor editor;

		const int without = editor.gutter_width ();

		editor.set_folding_enabled ( true );

		QVERIFY ( editor.gutter_width () > without );
	}

	void a_selection_opens_every_fold_around_its_node ()
	{
		// The reveal rule: tree navigation and Find reach the Code View through present(), and a node inside a fold
		// would otherwise be scrolled to and not shown -- the view would appear to do nothing.

		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );
		const FoldRegion element  = region_of ( QStringLiteral ( "/projects/1" ) );

		QVERIFY ( view->editor ()->toggle_fold ( element.openLine ) );
		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		const int caretBefore = view->editor ()->caret_line ();

		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects/1/status" ) ), SelectionOrigin::Tree );

		QVERIFY ( nothing_hidden () );
		QVERIFY ( folded ().isEmpty () );

		// Still the scroll-only channel: opening a fold is not moving the caret.

		QCOMPARE ( view->editor ()->caret_line (), caretBefore );
	}

	void a_reveal_leaves_unrelated_folds_alone ()
	{
		const FoldRegion profile  = region_of ( QStringLiteral ( "/profile" ) );
		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );
		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects/0/name" ) ), SelectionOrigin::Tree );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/profile" ) } ) );
		QVERIFY  ( folded_between ( profile ) );
	}

	void the_activation_gesture_opens_the_fold_it_lands_in ()
	{
		// Folded AFTER the selection, so the selection's own reveal has already happened and only the gesture is left to
		// open it -- the order a user meets by selecting a node, folding its container, then double-clicking the node.

		const FoldRegion profile = region_of ( QStringLiteral ( "/profile" ) );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/profile/country" ) ), SelectionOrigin::Tree );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );

		view->activate_editing ();

		const int caret = view->editor ()->caret_line ();

		QVERIFY ( view->editor ()->document ()->findBlockByNumber ( caret - 1 ).text ().contains ( QStringLiteral ( "country" ) ) );
		QVERIFY ( view->editor ()->is_line_visible ( caret ) );
	}

	void a_caret_that_lands_inside_a_fold_opens_it ()
	{
		// Right from the end of a fold's opening line lands INSIDE the fold -- Qt steps Up and Down over hidden lines
		// but not Right (measured on Qt 6.10.1). The rule is stated on where the caret ends up, so this is caught however
		// it got there.

		const FoldRegion profile = region_of ( QStringLiteral ( "/profile" ) );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( profile.openLine - 1 ) );

		cursor.movePosition ( QTextCursor::EndOfBlock );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Right );

		QVERIFY ( view->editor ()->is_line_visible ( view->editor ()->caret_line () ) );
		QVERIFY ( folded ().isEmpty () );
	}

	void folding_around_the_caret_moves_the_caret_to_the_opening_line ()
	{
		// Left where it was, the caret would sit in a hidden line -- and the rule above would open the fold again at once.

		const FoldRegion profile = region_of ( QStringLiteral ( "/profile" ) );

		view->editor ()->move_caret_to_line ( profile.openLine + 1 );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );

		QCOMPARE ( view->editor ()->caret_line (), profile.openLine );
		QVERIFY  ( folded_between ( profile ) );
	}

	void the_fold_keys_fold_outward_and_unfold_inward ()
	{
		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );
		const FoldRegion element  = region_of ( QStringLiteral ( "/projects/0" ) );

		view->editor ()->move_caret_to_line ( element.openLine + 1 );

		QTest::keyClick ( view->editor (), Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/0" ) } ) );
		QCOMPARE ( view->editor ()->caret_line (), element.openLine );

		// Pressed again, the next region out -- and in the brace spelling, which is what Shift turns the bracket key
		// into on some layouts.

		QTest::keyClick ( view->editor (), Qt::Key_BraceLeft, Qt::ControlModifier | Qt::ShiftModifier );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects" ), QStringLiteral ( "/projects/0" ) } ) );

		QTest::keyClick ( view->editor (), Qt::Key_BracketRight, Qt::ControlModifier | Qt::ShiftModifier );

		// The outer fold opens and the inner one is still folded, as it was left.

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/0" ) } ) );
		QVERIFY  ( folded_between ( element ) );

		view->editor ()->move_caret_to_line ( element.openLine );

		QTest::keyClick ( view->editor (), Qt::Key_BraceRight, Qt::ControlModifier | Qt::ShiftModifier );

		QVERIFY ( nothing_hidden () );
	}

	void a_scroll_to_a_line_below_a_fold_counts_visible_lines ()
	{
		// The scroll bar counts VISIBLE lines once anything is folded. A target placed by its block number instead
		// lands the viewport as many lines too far down as the fold above it hides.

		QString json = QStringLiteral ( "{ \"big\": [ " );

		for ( int i = 0; i < 300; ++i )
		{
			json += QString::number ( i ) + ( ( i < 299 ) ? QStringLiteral ( ", " ) : QStringLiteral ( " ], " ) );
		}

		json += QStringLiteral ( "\"target\": 1, \"more\": [ " );

		for ( int i = 0; i < 300; ++i )
		{
			json += QString::number ( i ) + ( ( i < 299 ) ? QStringLiteral ( ", " ) : QStringLiteral ( " ] }" ) );
		}

		set_document ( json );

		show_view ();

		// The scroll RANGE shrinks by exactly the lines the fold hides. Hiding a block does not do that by itself --
		// measured on Qt 6.10.1, the layout keeps counting a hidden block until its range is marked dirty.

		const FoldRegion big           = region_of ( QStringLiteral ( "/big" ) );
		const int        rangeUnfolded = view->editor ()->verticalScrollBar ()->maximum ();

		QVERIFY ( view->editor ()->toggle_fold ( big.openLine ) );

		QCOMPARE ( view->editor ()->verticalScrollBar ()->maximum (), rangeUnfolded - ( big.closeLine - big.openLine - 1 ) );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/target" ) ), SelectionOrigin::Tree );

		const QRect target = view->editor ()->cursorRect
		(
			QTextCursor ( view->editor ()->document ()->findBlockByNumber ( line_containing ( QStringLiteral ( "\"target\"" ) ) - 1 ) )
		);

		QVERIFY2
		(
			( target.top () >= 0 ) && ( target.bottom () <= view->editor ()->viewport ()->height () ),
			qPrintable ( QStringLiteral ( "the target is at y = %1 in a viewport %2 tall" ).arg ( target.top () ).arg ( view->editor ()->viewport ()->height () ) )
		);
	}

	void folds_stay_on_their_nodes_across_a_refresh ()
	{
		// An edit elsewhere regenerates the whole text, and every block is new. The fold is found again by its NODE's
		// pointer -- here two lines further down than it was, because the edit grew an object above it.

		const FoldRegion before = region_of ( QStringLiteral ( "/projects" ) );

		QVERIFY ( view->editor ()->toggle_fold ( before.openLine ) );

		ParseResult bigger = JsonParser::parse
		(
			QStringLiteral ( "{ \"city\": \"Cape Town\", \"country\": \"ZA\", \"street\": \"Long\", \"zip\": \"8001\" }" )
		);

		QVERIFY ( bigger.ok );

		undo->replace_subtree ( JsonPointer::parse ( QStringLiteral ( "/profile" ) ), std::move ( bigger.root ), QStringLiteral ( "Edit" ) );

		const FoldRegion after = region_of ( QStringLiteral ( "/projects" ) );

		QCOMPARE ( after.openLine, before.openLine + 2 );
		QVERIFY  ( folded_between ( after ) );
		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects" ) } ) );

		// And an undo, which regenerates the text again, keeps it too.

		undo->undo ();

		QVERIFY ( folded_between ( region_of ( QStringLiteral ( "/projects" ) ) ) );
	}

	void a_fold_follows_its_node_when_another_view_moves_it ()
	{
		// An array element's pointer is its POSITION, so an element inserted above a folded one -- from the tree, say --
		// renumbers it. Found again by pointer alone, the fold would land on the neighbour that took its old number.

		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		ParseResult inserted = JsonParser::parse ( QStringLiteral ( "{ \"name\": \"Inserted\", \"status\": \"new\" }" ) );

		QVERIFY ( inserted.ok );

		undo->insert_element_at ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), 0, std::move ( inserted.root ), QStringLiteral ( "Insert" ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/2" ) } ) );
		QVERIFY  ( folded_between ( region_of ( QStringLiteral ( "/projects/2" ) ) ) );
		QVERIFY  ( view->editor ()->toPlainText ().contains ( QStringLiteral ( "Data Migration" ) ) );

		// Undone, it moves back -- and a rename of a key above it carries it along too.

		undo->undo ();

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/1" ) } ) );

		undo->rename_key ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), QStringLiteral ( "work" ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/work/1" ) } ) );
		QVERIFY  ( folded_between ( region_of ( QStringLiteral ( "/work/1" ) ) ) );
	}

	void a_fold_follows_its_node_after_a_commit ()
	{
		// A commit builds a NEW tree, so the node noted for a fold before it is not in the document afterwards. The
		// commit is also the moment the text and the document agree again, and the nodes are noted afresh there.

		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		// A change of VALUE, so the commit really does replace the tree -- a whitespace-only edit parses to the tree the
		// document already has, and replace_subtree answers Unchanged without replacing anything.

		const int nameAt = view->editor ()->toPlainText ().indexOf ( QStringLiteral ( "Alex Rivera" ) );

		QTextCursor cursor ( view->editor ()->document () );

		cursor.setPosition ( nameAt );
		cursor.insertText ( QStringLiteral ( "Dr " ) );

		QVERIFY ( view->commit_now () );
		QVERIFY ( document->root ()->find_member ( QStringLiteral ( "name" ) )->string_value ().startsWith ( QStringLiteral ( "Dr " ) ) );

		ParseResult inserted = JsonParser::parse ( QStringLiteral ( "{ \"name\": \"Inserted\", \"status\": \"new\" }" ) );

		QVERIFY ( inserted.ok );

		undo->insert_element_at ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), 0, std::move ( inserted.root ), QStringLiteral ( "Insert" ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/2" ) } ) );
	}

	void a_fold_made_during_an_edit_follows_its_node_after_esc ()
	{
		// While the text is the user's, a pointer read off it may not name the document's node, so nothing is noted
		// then. Esc makes the two agree again, and the fold made in between is noted there.

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( 2 ) );

		cursor.movePosition ( QTextCursor::EndOfBlock );
		cursor.insertText ( QStringLiteral ( " " ) );

		QVERIFY ( view->has_uncommitted_edit () );
		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		view->discard_edit ();

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/1" ) } ) );

		ParseResult inserted = JsonParser::parse ( QStringLiteral ( "{ \"name\": \"Inserted\", \"status\": \"new\" }" ) );

		QVERIFY ( inserted.ok );

		undo->insert_element_at ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), 0, std::move ( inserted.root ), QStringLiteral ( "Insert" ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/2" ) } ) );
	}

	void a_fold_inside_a_replaced_subtree_keeps_its_pointer ()
	{
		// A subtree REPLACED -- a type change, an array transform, here a replace with a different value -- has new
		// nodes throughout, so there is no identity to follow and no node to call deleted. The fold stays at its pointer,
		// which in a replacement of the same shape is the same place.

		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		ParseResult replacement = JsonParser::parse
		(
			QStringLiteral ( "[ { \"name\": \"Renamed\", \"status\": \"done\" }, { \"name\": \"Data Migration\", \"status\": \"completed\" } ]" )
		);

		QVERIFY ( replacement.ok );

		undo->replace_subtree ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), std::move ( replacement.root ), QStringLiteral ( "Replace" ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/1" ) } ) );
		QVERIFY  ( folded_between ( region_of ( QStringLiteral ( "/projects/1" ) ) ) );
	}

	void a_fold_whose_node_is_deleted_is_dropped ()
	{
		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		undo->delete_node ( JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ) );

		QVERIFY ( folded ().isEmpty () );
		QVERIFY ( nothing_hidden () );

		// Undoing the delete brings the node back, not the fold: a dropped fold is gone. Whereas deleting the element ABOVE a folded one moves the fold up with its node.

		undo->undo ();

		QVERIFY ( folded ().isEmpty () );
		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		undo->delete_node ( JsonPointer::parse ( QStringLiteral ( "/projects/0" ) ) );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/0" ) } ) );
		QVERIFY  ( view->editor ()->toPlainText ().contains ( QStringLiteral ( "Data Migration" ) ) );
	}

	void folds_travel_with_the_text_as_it_is_typed ()
	{
		// Typing moves the hidden lines with their text, so the fold stays on the lines it was put on -- here an array
		// element inserted ABOVE a folded one, which renumbers the folded one's pointer from /projects/1 to /projects/2.
		// Found again by pointer, the fold would have moved onto the element just typed.
		//
		// Inserted at the START of the fold's opening line, which is the case a mark on that line would get wrong: it
		// stays with the text before the split, which is the inserted line.

		const FoldRegion element = region_of ( QStringLiteral ( "/projects/1" ) );

		QVERIFY ( view->editor ()->toggle_fold ( element.openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( element.openLine - 1 ) );

		cursor.insertText ( QStringLiteral ( "    { \"name\": \"Inserted\" },\n" ) );

		QTest::qWait ( config::code::VALIDATION_DEBOUNCE + 60 );

		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects/2" ) } ) );

		const FoldRegion moved = region_of ( QStringLiteral ( "/projects/2" ) );

		QCOMPARE ( moved.openLine, element.openLine + 1 );
		QVERIFY  ( folded_between ( moved ) );
		QVERIFY  ( view->editor ()->is_line_visible ( element.openLine ) );
	}

	void an_edit_inside_a_fold_opens_it ()
	{
		// Enter at the start of the fold's closing line makes a new line BETWEEN its brackets, visible and under the
		// caret. Keeping the fold would hide it the moment the user paused.

		const FoldRegion profile = region_of ( QStringLiteral ( "/profile" ) );

		QVERIFY ( view->editor ()->toggle_fold ( profile.openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( profile.closeLine - 1 ) );

		view->editor ()->setTextCursor ( cursor );

		QTest::keyClick ( view->editor (), Qt::Key_Return );

		QTest::qWait ( config::code::VALIDATION_DEBOUNCE + 60 );

		QVERIFY ( nothing_hidden () );
		QVERIFY ( folded ().isEmpty () );
	}

	void folds_wait_while_the_text_does_not_parse ()
	{
		// Below a syntax error the index is silent, and silence is not deletion: a fold down there stays folded until the
		// text parses again, rather than opening on every pause and closing again when the typing is done.

		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( 1 ) );

		cursor.movePosition ( QTextCursor::EndOfBlock );
		cursor.insertText ( QStringLiteral ( " ,,," ) );

		QTest::qWait ( config::code::VALIDATION_DEBOUNCE + 60 );

		QVERIFY ( !view->is_text_valid () );
		QVERIFY ( folded_between ( projects ) );

		view->editor ()->undo ();

		QTest::qWait ( config::code::VALIDATION_DEBOUNCE + 60 );

		QVERIFY  ( view->is_text_valid () );
		QVERIFY  ( folded_between ( projects ) );
		QCOMPARE ( folded (), QStringList ( { QStringLiteral ( "/projects" ) } ) );
	}

	void a_load_opens_every_fold ()
	{
		// A load shares no nodes with the document before it, only -- by accident -- some pointers. Loading the same
		// shape again is the case where a fold carried by pointer would survive, so it is the case asserted.

		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects" ) ).openLine ) );

		set_document ( QString::fromUtf8 ( SAMPLE_DOCUMENT ) );

		QVERIFY ( nothing_hidden () );
		QVERIFY ( folded ().isEmpty () );
	}

	void esc_discards_the_edit_and_keeps_the_folds ()
	{
		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );
		const QString    original = view->editor ()->toPlainText ();

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( 2 ) );

		cursor.movePosition ( QTextCursor::EndOfBlock );
		cursor.insertText ( QStringLiteral ( " " ) );

		QVERIFY ( view->has_uncommitted_edit () );

		view->discard_edit ();

		QCOMPARE ( view->editor ()->toPlainText (), original );
		QVERIFY  ( folded_between ( projects ) );
	}

	void what_is_printed_is_what_is_shown ()
	{
		// FILE-12: the page is the view's rendering, and the rendering has the fold in it.

		const FoldRegion projects = region_of ( QStringLiteral ( "/projects" ) );

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		const QString printed = view->print_content ( 0 ).text;

		QVERIFY ( printed.contains ( QStringLiteral ( "\"projects\"" ) ) );
		QVERIFY ( !printed.contains ( QStringLiteral ( "JSON Editor" ) ) );

		QCOMPARE ( printed.split ( QLatin1Char ( '\n' ) ).size (), view->editor ()->blockCount () - ( projects.closeLine - projects.openLine - 1 ) );

		QVERIFY ( view->editor ()->toggle_fold ( projects.openLine ) );

		QCOMPARE ( view->print_content ( 0 ).text, view->editor ()->toPlainText () );
	}

	void what_is_printed_follows_the_screen_when_the_folds_move_ahead_of_it ()
	{
		// Another view deletes a folded node while this one holds an uncommitted edit: the fold is dropped from the set
		// at once, but the refresh that would show the change waits behind the edit (EDITOR-09) -- so the lines are
		// still hidden on screen, and the page has to agree with the screen rather than with the set.

		QVERIFY ( view->editor ()->toggle_fold ( region_of ( QStringLiteral ( "/projects/1" ) ).openLine ) );

		QTextCursor cursor ( view->editor ()->document ()->findBlockByNumber ( 2 ) );

		cursor.movePosition ( QTextCursor::EndOfBlock );
		cursor.insertText ( QStringLiteral ( " " ) );

		undo->delete_node ( JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ) );

		QVERIFY ( view->has_uncommitted_edit () );
		QVERIFY ( folded ().isEmpty () );

		QVERIFY ( !view->print_content ( 0 ).text.contains ( QStringLiteral ( "Data Migration" ) ) );
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

void TestCodeView::an_uncommitted_edit_is_reported_as_unsaved_work ()
{
	// VIEW-04. The title's marker asks the DOCUMENT whether there is unsaved work, and until 2026-08-22 that was the
	// whole of the question -- so typing in the Code View left the title claiming there was nothing to lose, right up
	// until the edit was committed. The document's own dirty flag is deliberately unchanged (EDITOR-09's gate already
	// asks keep / discard on the way out, and UNDO-04 binds the flag to the undo stack's clean state).
	//
	// Written as an OPPOSING TRIO -- clean, dirty, clean again -- because asserting only the middle state passes
	// against a view that answers true unconditionally.

	// The fixture already installed SAMPLE_DOCUMENT and the view already holds its formatted text, so the starting
	// state is a committed buffer -- which is the "clean" the trio needs.

	QVERIFY ( !view->has_unsaved_view_edit () );

	QSignalSpy changes ( view.get (), &CodeView::unsaved_view_edit_changed );

	set_editor_text ( QStringLiteral ( "{\"a\":2}" ) );

	QVERIFY ( view->has_unsaved_view_edit () );
	QVERIFY ( changes.count () > 0 );

	// The DOCUMENT is untouched -- this is unsaved work the document has not heard about, which is exactly the state
	// the marker had no way to report.

	QVERIFY ( !document->is_dirty () );

	// Committing closes the gap, and says so rather than leaving the title to find out on the next keystroke.

	changes.clear ();

	QVERIFY ( view->commit_now () );

	QVERIFY  ( !view->has_unsaved_view_edit () );
	QVERIFY  ( changes.count () > 0 );
	QVERIFY  ( document->is_dirty () );                  // Now it IS the document's, which is the other half.

	// And a discard closes it too, from the other direction.

	set_editor_text ( QStringLiteral ( "{\"a\":3}" ) );

	QVERIFY ( view->has_unsaved_view_edit () );

	view->discard_edit ();

	QVERIFY ( !view->has_unsaved_view_edit () );
}

#include "tst_code_view.moc"

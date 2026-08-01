//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for TextView -- the read-only plain-text rendering (EDITOR-06).
//
//   WHAT IS NOT COVERED HERE, DELIBERATELY. The eight table styles, the alignment, and the Markdown forms are
//   TextViewRenderer's, and tst_text_view_renderer already matches all of them byte-for-byte against the spec. Testing
//   them again through the widget would assert the same strings twice and pin nothing new. What is asserted here is
//   only what the WIDGET adds:
//
//     - THE GOLDEN OUTPUT ARRIVES INTACT (the development plan's "golden Text View output wired through the UI"). One
//       comparison against the renderer's own output, which is what makes the wiring -- and not the rendering -- the
//       thing under test.
//     - WHICH NODE IS RENDERED (EDITOR-06). A scalar selection renders its PARENT's listing, because the Text View is
//       defined as a rendering of the FORM VIEW'S PRESENTATION. This is the claim that would silently regress if the
//       shared presentation rule were ever re-implemented here.
//     - IT IS READ-ONLY BUT SELECTABLE. Copying the rendering elsewhere is the entire purpose of the view, so losing
//       keyboard selection would defeat it while leaving it looking correct.
//     - THE SET-06 PROFILE IS LIVE. A settings change re-renders; an UNRELATED settings change does not.
//     - EDITOR-08. An edit committed elsewhere shows here without a reload.
//     - THE SCROLL POSITION SURVIVES a re-render of the node already on screen, which is what arrowing down an object's
//       scalars does on every keystroke.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"
#include "style/fixed_font.hpp"
#include "views/TextView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/TextViewRenderer.hpp>

#include <QtTest/QtTest>

#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"id": 1001,
		"name": "Alex Rivera",
		"profile": { "city": "Cape Town" },
		"roles": [ "admin", "editor" ],
		"projects":
		[
			{ "name": "JSON Editor",    "status": "in-progress" },
			{ "name": "Data Migration", "status": "completed"   }
		]
	})";
}

class TestTextView : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir>  settingsDirectory;
	std::unique_ptr<SettingsStore>  settings;
	std::unique_ptr<JsonDocument>   document;
	std::unique_ptr<UndoController> undo;
	std::unique_ptr<TextView>       view;

	void build_fixture ()
	{
		// Torn down in strict reverse dependency order: the view watches the document and the settings, so
		// it must go first. Resetting the members in this order is what does that.

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
		view = std::make_unique<TextView> ( document.get (), settings.get () );
	}

	// Replace the fixture's document with one a case authored for itself, keeping the same view and settings.

	void load ( const char* text )
	{
		ParseResult parsed = JsonParser::parse ( QString::fromUtf8 ( text ) );

		QVERIFY ( parsed.ok );

		document->set_root ( std::move ( parsed.root ) );
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
	// The rendering arrives intact.
	//=================================================================================================================

	void the_widget_shows_exactly_what_the_renderer_produces ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		const QString expected = TextViewRenderer::render ( *document->root (), text_view_profile ( settings.get () ) );

		QCOMPARE ( view->rendered_text (), expected );
	}

	void an_array_selection_renders_the_table ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

		const QString expected = TextViewRenderer::render
		(
			*document->resolve ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ) ),
			text_view_profile ( settings.get () )
		);

		QCOMPARE ( view->rendered_text (), expected );
	}

	//=================================================================================================================
	// Which node is rendered (EDITOR-06's "the Form View's presentation").
	//=================================================================================================================

	void a_scalar_selection_renders_its_parents_listing ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/profile/city" ) ), SelectionOrigin::Tree );

		// The PARENT object, not the lone scalar -- the same resolution the Form View makes, from the same rule.

		QCOMPARE ( view->rendered_pointer (), JsonPointer::parse ( QStringLiteral ( "/profile" ) ) );

		QVERIFY ( view->rendered_text ().contains ( QStringLiteral ( "city" ) ) );
	}

	void an_element_of_an_array_of_objects_renders_the_array_table ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects/1/status" ) ), SelectionOrigin::Tree );

		QCOMPARE ( view->rendered_pointer (), JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ) );
	}

	void an_unresolvable_pointer_clears_the_view ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QVERIFY ( !view->rendered_text ().isEmpty () );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/nope/at/all" ) ), SelectionOrigin::Tree );

		QVERIFY ( view->rendered_text ().isEmpty () );
	}

	//=================================================================================================================
	// Printing (FILE-12).
	//=================================================================================================================

	void what_is_printed_is_the_same_rendering_at_the_page_s_width ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

		const PrintContent content = view->print_content ( 0 );

		// PREFORMATTED, not a table: TextViewRenderer aligns its columns and draws its rules by counting characters,
		// so the printed page has to be set in a fixed-width font or it stops meaning what it showed (EDITOR-06).

		QCOMPARE ( content.kind, PrintContent::Kind::Preformatted );

		// It is this renderer's output through the same profile reader the pane uses -- so the print and the pane
		// cannot render a node two different ways. What differs is the WIDTH, and only the width.

		TextViewProfile profile = text_view_profile ( settings.get () );

		profile.wrapColumns = 0;

		QCOMPARE
		(
			content.text,
			TextViewRenderer::render
			(
				*document->resolve ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ) ),
				profile
			)
		);

		QCOMPARE ( content.subject, QStringLiteral ( "/projects" ) );

		QVERIFY2 ( !content.viewName.isEmpty (), "the rendering must name itself for the status message" );
	}

	void the_hanging_indent_is_honoured_at_the_page_s_width_not_the_pane_s ()
	{
		// The reported defect, stated as a case. The pane wraps to its own viewport; a page narrower in characters used
		// to re-break each of those lines at column 0, so a printed entry alternated between continuations under the
		// value column and continuations against the margin. Asking the view for the rendering AT the page's width is
		// what fixes it, and this asserts the consequence: every continuation lines up under the value column.

		constexpr int PAGE_COLUMNS = 60;

		load ( R"({ "shortText1": "big-test-test",
		            "longText1": "Most JSON editors hand you a wall of text and leave the structure for you to hold in your head, which is the problem this program exists to solve.",
		            "count": 0 })" );

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		const QStringList lines = view->print_content ( PAGE_COLUMNS ).text.split ( QLatin1Char ( '\n' ) );

		// The value column is where the first line's value starts, taken from the rendering rather than counted by
		// hand -- the separator and its alignment are settings, so a literal here would pin the wrong thing.

		int valueColumn = -1;

		for ( const QString& line : lines )
		{
			if ( line.startsWith ( QStringLiteral ( "longText1" ) ) )
			{
				valueColumn = static_cast<int> ( line.indexOf ( QStringLiteral ( "Most" ) ) );

				break;
			}
		}

		QVERIFY2 ( valueColumn > 0, qPrintable ( lines.join ( QLatin1Char ( '\n' ) ) ) );

		int continuations = 0;

		for ( const QString& line : lines )
		{
			if ( line.isEmpty () || !line.startsWith ( QLatin1Char ( ' ' ) ) )
			{
				continue;
			}

			++continuations;

			QCOMPARE ( static_cast<int> ( line.length () - QStringView { line }.trimmed ().length () ), valueColumn );
		}

		QVERIFY2 ( continuations > 0, qPrintable ( lines.join ( QLatin1Char ( '\n' ) ) ) );

		// And every line fits the page, which is the other half: an entry wrapped to the PANE's width would still have
		// lines the page has to break again.

		for ( const QString& line : lines )
		{
			QVERIFY2
			(
				line.length () <= PAGE_COLUMNS,
				qPrintable ( QStringLiteral ( "%1 characters: %2" ).arg ( line.length () ).arg ( line ) )
			);
		}
	}

	void the_page_width_is_used_whatever_wrap_strings_says ()
	{
		// SET-06 is off by default and governs the PANE. A page has a hard physical width and no horizontal scrolling,
		// so the print wraps regardless -- otherwise the reported defect simply returns for everyone who never turned
		// the setting on.

		QVERIFY ( !wrap_strings_in_text_view ( settings.get () ) );

		load ( R"({ "longText1": "Most JSON editors hand you a wall of text and leave the structure for you to hold in your head, which is the problem this program exists to solve." })" );

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		const QStringList lines = view->print_content ( 60 ).text.split ( QLatin1Char ( '\n' ) );

		QVERIFY2 ( lines.size () > 1, qPrintable ( lines.join ( QLatin1Char ( '\n' ) ) ) );
	}

	void a_rendering_that_must_not_be_wrapped_is_clipped_instead ()
	{
		// CSV, TSV, the Markdown table and the five box styles are renderings where a broken line is a CORRUPT record
		// rather than an untidy one, so the page must not wrap them -- and the view says so rather than the page
		// guessing from the text (which would confuse "this style does not wrap" with "this one line could not be").

		settings->set_string ( settings_keys::TEXT_TABLE_STYLE, settings_values::TABLE_STYLE_CSV );

		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

		QCOMPARE ( view->print_content ( 40 ).overflow, PrintContent::Overflow::Clip );

		// The key-value listing wraps, so it is never clipped -- nothing on that page can be lost off the margin.

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QCOMPARE ( view->print_content ( 40 ).overflow, PrintContent::Overflow::Wrap );
	}

	void a_view_with_nothing_rendered_prints_nothing ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/nope/at/all" ) ), SelectionOrigin::Tree );

		QVERIFY ( view->print_content ( 60 ).is_empty () );
	}

	//=================================================================================================================
	// It is a reading surface, not an editing one.
	//=================================================================================================================

	void the_text_is_read_only_but_selectable ()
	{
		QVERIFY ( view->text_edit ()->isReadOnly () );

		// Selectable by KEYBOARD as well as by mouse: a user copying a rendering into a document is as likely to reach
		// for Ctrl+A as for a drag, and read-only alone does not guarantee the keyboard half.

		QVERIFY ( view->text_edit ()->textInteractionFlags ().testFlag ( Qt::TextSelectableByKeyboard ) );
		QVERIFY ( view->text_edit ()->textInteractionFlags ().testFlag ( Qt::TextSelectableByMouse ) );
	}

	void the_font_is_fixed_width ()
	{
		// A correctness constraint, not a preference: the renderer aligns separators and draws table rules by counting
		// characters, so a proportional font leaves every aligned column ragged and every border broken.
		//
		// Asserted as the PROPERTY -- every character the same width -- rather than as QFontInfo::fixedPitch(), which
		// reports the matched family's own metadata. This is the check that caught the real defect: asking Qt for
		// QFontDatabase::systemFont(FixedFont) returned the generic ALIAS family "monospace", which resolved on this
		// machine to "Agency FB" -- a narrow PROPORTIONAL face that would have left every aligned column ragged while
		// looking like a deliberate choice of a condensed font.

		QVERIFY2 ( measures_fixed_width ( view->text_edit ()->font () ),
		           qPrintable ( QStringLiteral ( "resolved to the proportional family '%1'" )
		                        .arg ( QFontInfo ( view->text_edit ()->font () ).family () ) ) );
	}

	//=================================================================================================================
	// The SET-06 profile is live.
	//=================================================================================================================

	void a_text_view_setting_re_renders ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		const QString before = view->rendered_text ();

		settings->set_string ( settings_keys::TEXT_NAME_SEPARATOR, QStringLiteral ( "=" ) );

		QVERIFY ( view->rendered_text () != before );
		QVERIFY ( view->rendered_text ().contains ( QLatin1Char ( '=' ) ) );
	}

	void the_table_style_setting_reaches_the_renderer ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

		settings->set_string ( settings_keys::TEXT_TABLE_STYLE, settings_values::TABLE_STYLE_MARKDOWN );

		TextViewProfile markdown;

		markdown.tableStyle = TableStyle::Markdown;

		QCOMPARE
		(
			view->rendered_text (),
			TextViewRenderer::render ( *document->resolve ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ) ), markdown )
		);
	}

	void the_string_display_setting_re_renders ()
	{
		// SET-03 is the ONE setting outside the textView.* group that this view reads, so the group filter had to name
		// it explicitly. It did not, so changing String display left the Text View showing the old notation until some
		// unrelated textView.* write happened to re-render it -- while the Form View, which watches its own keys,
		// changed immediately. Two tabs, one setting, two different renderings (2026-07-28 review).

		const auto tabbed = JsonParser::parse ( QStringLiteral ( "{\"note\":\"a\\tb\"}" ) );

		document->set_root ( tabbed.root->clone () );

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		settings->set_string ( settings_keys::STRING_DISPLAY, settings_values::STRING_DISPLAY_ESCAPED );

		QVERIFY2 ( view->rendered_text ().contains ( QStringLiteral ( "a\\tb" ) ),
		           qPrintable ( view->rendered_text () ) );

		settings->set_string ( settings_keys::STRING_DISPLAY, settings_values::STRING_DISPLAY_DECODED );

		QVERIFY2 ( view->rendered_text ().contains ( QStringLiteral ( "a\tb" ) ),
		           qPrintable ( view->rendered_text () ) );
	}

	void an_unrelated_setting_does_not_re_render ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		// Filtered to the textView.* group. Without the filter, writing the window geometry -- which happens on every
		// resize -- would re-render a large node on each one.

		const QString before = view->rendered_text ();

		settings->set_string ( settings_keys::THEME, QStringLiteral ( "Dark" ) );

		QCOMPARE ( view->rendered_text (), before );
	}

	//=================================================================================================================
	// EDITOR-08, and the scroll position.
	//=================================================================================================================

	void an_edit_made_elsewhere_shows_without_a_reload ()
	{
		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QVERIFY ( view->rendered_text ().contains ( QStringLiteral ( "Alex Rivera" ) ) );

		undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), QStringLiteral ( "Sam Patel" ) );

		QVERIFY ( view->rendered_text ().contains ( QStringLiteral ( "Sam Patel" ) ) );
		QVERIFY ( !view->rendered_text ().contains ( QStringLiteral ( "Alex Rivera" ) ) );
	}

	void re_presenting_the_same_container_keeps_the_scroll_position ()
	{
		view->text_edit ()->resize ( 400, 60 );

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QScrollBar* const scrollBar = view->text_edit ()->verticalScrollBar ();

		if ( scrollBar->maximum () == 0 )
		{
			QSKIP ( "the rendering fits the viewport, so there is no scroll offset to preserve" );
		}

		scrollBar->setValue ( scrollBar->maximum () );

		const int offset = scrollBar->value ();

		// A SCALAR member of the root resolves back to the root's own listing -- which is exactly what every arrow key
		// down the tree does while walking an object's members, and is why this must not walk the reader back to the
		// top on each one.

		view->present ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );

		QCOMPARE ( view->rendered_pointer (), JsonPointer () );
		QCOMPARE ( scrollBar->value (), offset );
	}

	//=================================================================================================================
	// The provider.
	//=================================================================================================================

	void the_provider_sits_between_form_and_code ()
	{
		const TextViewProvider provider ( document.get (), settings.get () );

		QCOMPARE ( provider.view_id (), QStringLiteral ( "text" ) );
		QCOMPARE ( provider.display_order (), 1 );

		QVERIFY ( provider.can_present ( document->root () ) );
		QVERIFY ( !provider.can_present ( nullptr ) );

		QVERIFY2 ( !provider.icon_name ().isEmpty (), "the view tab glyphs landed in Phase 8 (STYLE-06)" );
	}
	//-----------------------------------------------------------------------------------------------------------------
	// Wrap strings (SET-06)
	//-----------------------------------------------------------------------------------------------------------------

	void the_wrap_width_comes_from_the_viewport ()
	{
		// The one thing that connects this widget to the renderer's wrapping: a width in PIXELS becomes a width in
		// CHARACTERS, exactly, because the font is fixed-width -- the same property the renderer's column alignment
		// already depends on. Asserted by making the view narrow and checking that no rendered
		// line overruns it, which is the claim a user actually cares about.

		ParseResult longValue = JsonParser::parse ( QStringLiteral (
			R"({"note":"Most JSON editors hand you a wall of text and leave the structure for you to hold in your head."})" ) );

		document->set_root ( std::move ( longValue.root ) );

		view->resize ( 640, 400 );
		view->show ();

		settings->set_bool ( settings_keys::TEXT_WRAP_STRINGS, true );

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		const QStringList wide = view->rendered_text ().split ( QLatin1Char ( '\n' ) );

		QVERIFY2 ( wide.size () > 1, "A long value did not wrap at all" );

		// Narrower window, more lines -- the wrap follows the window, which is what makes it a resize concern rather
		// than a fixed margin.

		view->resize ( 360, 400 );

		QCoreApplication::processEvents ();

		const QStringList narrow = view->rendered_text ().split ( QLatin1Char ( '\n' ) );

		QVERIFY2 ( narrow.size () > wide.size (), "Narrowing the view did not produce more lines" );

		// The hanging indent held all the way down: every continuation starts under the value column.

		const int indentWidth = narrow.first ().indexOf ( QStringLiteral ( ": " ) ) + 2;

		for ( int index = 1; index < narrow.size (); ++index )
		{
			QVERIFY2 ( narrow [ index ].startsWith ( QString ( indentWidth, QLatin1Char ( ' ' ) ) ),
			           qPrintable ( QStringLiteral ( "continuation not indented: %1" ).arg ( narrow [ index ] ) ) );
		}
	}

	void wrapping_off_leaves_the_value_on_one_line ()
	{
		// Off by default (SET-06), and off means off however narrow the window is.

		ParseResult longValue = JsonParser::parse ( QStringLiteral (
			R"({"note":"Most JSON editors hand you a wall of text and leave the structure for you to hold in your head."})" ) );

		document->set_root ( std::move ( longValue.root ) );

		view->resize ( 300, 400 );
		view->show ();

		view->present ( JsonPointer (), SelectionOrigin::Tree );

		QCOMPARE ( view->rendered_text ().count ( QLatin1Char ( '\n' ) ), 0 );
	}

	// NFR-05. The tab names the view on screen, but the keyboard lands INSIDE the view, and that is where a screen
	// reader asks what it is looking at.

	void the_view_carries_an_accessible_name ()
	{
		QPlainTextEdit* const surface = view->findChild<QPlainTextEdit*> ();

		QVERIFY ( surface != nullptr );
		QVERIFY2 ( !surface->accessibleName ().isEmpty (), "The Text View surface has no accessible name" );
	}
};

QTEST_MAIN ( TestTextView )

#include "tst_text_view.moc"

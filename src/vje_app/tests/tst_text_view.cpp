//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		const QString expected = TextViewRenderer::render ( *document->root (), TextViewProfile () );

		QCOMPARE ( view->rendered_text (), expected );
	}

	void an_array_selection_renders_the_table ()
	{
		view->present ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

		const QString expected = TextViewRenderer::render
		(
			*document->resolve ( JsonPointer::parse ( QStringLiteral ( "/projects" ) ) ),
			TextViewProfile ()
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
};

QTEST_MAIN ( TestTextView )

#include "tst_text_view.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for EditorPane -- the pluggable view registry and the tab strip it drives
//   (EDITOR-01, EDITOR-10).
//
//   Driven by STUB views rather than by the real Form View, deliberately. What is under test is the seam -- ordering,
//   applicability filtering, routing, and tab persistence -- and a stub is the only way to assert that the pane needs
//   nothing from a view beyond the contract. If these cases pass against a stub, the promise that "a fourth view is one
//   provider class and one registration line" is a checked claim rather than an intention.
//
//   The case that earns its place is TAB PERSISTENCE BY ID (EDITOR-10). Preserving the active tab by INDEX looks
//   identical until the applicable set changes -- which is exactly the moment a rebuild happens -- and then silently
//   lands the user on a different view.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/EditorPane.hpp"
#include "views/IEditorView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QLabel>
#include <QTabWidget>

#include <memory>

using namespace vje;

namespace
{
	// One object and one array at the top level, so a selection can move between a node the object-only stub view can
	// present and one it cannot.

	const char* const SAMPLE_DOCUMENT = R"({ "object": { "a": 1 }, "list": [ 1, 2 ] })";

	//-----------------------------------------------------------------------------------------------------------------
	// A view that records what it was asked to present and nothing else -- the whole IEditorView contract, and no more.
	//-----------------------------------------------------------------------------------------------------------------

	class StubView : public QLabel, public IEditorView
	{
	public:

		explicit StubView ( QWidget* parent )
			: QLabel ( parent )
		{
		}

		QWidget* widget () override
		{
			return this;
		}

		void present ( const JsonPointer& pointer, SelectionOrigin origin ) override
		{
			presentedPointer = pointer;
			presentedOrigin  = origin;

			++presentCount;
		}

		void activate_editing () override
		{
			++activateCount;
		}

		JsonPointer     presentedPointer;
		SelectionOrigin presentedOrigin = SelectionOrigin::Programmatic;

		int presentCount  = 0;
		int activateCount = 0;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A provider whose applicability and order the test sets directly.
	//-----------------------------------------------------------------------------------------------------------------

	class StubProvider : public IEditorViewProvider
	{
	public:

		StubProvider ( const QString& identifier, int order, bool presentsObjectsOnly )
			: identifier          ( identifier )
			, order               ( order )
			, presentsObjectsOnly ( presentsObjectsOnly )
		{
		}

		QString view_id       () const override { return identifier; }
		QString display_name  () const override { return identifier.toUpper (); }
		QString icon_name     () const override { return QString (); }
		int     display_order () const override { return order; }

		bool can_present ( const JsonNode* node ) const override
		{
			if ( node == nullptr )
			{
				return false;
			}

			return presentsObjectsOnly ? ( node->kind () == JsonKind::Object ) : true;
		}

		IEditorView* create_view ( QWidget* parent ) const override
		{
			lastCreated = new StubView ( parent );

			return lastCreated;
		}

		QString identifier;
		int     order;
		bool    presentsObjectsOnly;

		mutable StubView* lastCreated = nullptr;
	};
}

class TestEditorPane : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void tabs_appear_in_display_order ();
	void an_inapplicable_view_has_no_tab ();
	void an_empty_document_offers_no_tabs ();

	void the_selection_is_routed_to_the_visible_view_only ();
	void switching_tabs_presents_the_newly_visible_view ();

	void the_active_tab_survives_a_rebuild_by_id ();

	void activation_reaches_the_visible_view ();

private:

	void load ( const char* text );

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<EditorPane>       pane;

	// All owned by the pane; kept here to reach the views they create.

	StubProvider* alphaProvider = nullptr;   // Objects only -- the one whose tab comes and goes.
	StubProvider* betaProvider  = nullptr;
	StubProvider* gammaProvider = nullptr;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::init ()
{
	document  = std::make_unique<JsonDocument> ();
	selection = std::make_unique<SelectionService> ();
	pane      = std::make_unique<EditorPane> ( document.get (), selection.get (), nullptr );

	// THREE views, registered out of display order on purpose. Three rather than two because the tab-persistence rule
	// only becomes distinguishable from an index-based one when a view that is NOT the first can survive a rebuild that
	// removes a view before it -- see the_active_tab_survives_a_rebuild_by_id.

	auto gamma = std::make_unique<StubProvider> ( QStringLiteral ( "gamma" ), 20, false );
	auto alpha = std::make_unique<StubProvider> ( QStringLiteral ( "alpha" ),  0, true );
	auto beta  = std::make_unique<StubProvider> ( QStringLiteral ( "beta" ),  10, false );

	gammaProvider = gamma.get ();
	alphaProvider = alpha.get ();
	betaProvider  = beta.get ();

	pane->register_view ( std::move ( gamma ) );
	pane->register_view ( std::move ( alpha ) );
	pane->register_view ( std::move ( beta ) );
}

void TestEditorPane::cleanup ()
{
	pane.reset ();
	selection.reset ();
	document.reset ();
}

void TestEditorPane::load ( const char* text )
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	document->set_root ( std::move ( result.root ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The tab strip
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::tabs_appear_in_display_order ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	// Registration order was gamma, alpha, beta; display order is what the strip shows.

	QCOMPARE ( pane->tabs ()->count (), 3 );
	QCOMPARE ( pane->tabs ()->tabText ( 0 ), QStringLiteral ( "ALPHA" ) );
	QCOMPARE ( pane->tabs ()->tabText ( 1 ), QStringLiteral ( "BETA" ) );
	QCOMPARE ( pane->tabs ()->tabText ( 2 ), QStringLiteral ( "GAMMA" ) );

	// The default tab is the first in display order (EDITOR-01: Form View is the default).

	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "alpha" ) );
}

void TestEditorPane::an_inapplicable_view_has_no_tab ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/list" ) ), SelectionOrigin::Tree );

	// The object-only view cannot present an array, so its tab is absent rather than present-and-empty.

	QCOMPARE ( pane->tabs ()->count (), 2 );
	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "beta" ) );
	QVERIFY  ( pane->view_for_id ( QStringLiteral ( "alpha" ) ) == nullptr );
}

void TestEditorPane::an_empty_document_offers_no_tabs ()
{
	QCOMPARE ( pane->tabs ()->count (), 0 );
	QCOMPARE ( pane->active_view_id (), QString () );
}

//---------------------------------------------------------------------------------------------------------------------
// Routing
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::the_selection_is_routed_to_the_visible_view_only ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/object" ) ), SelectionOrigin::GoTo );

	StubView* const visibleView = alphaProvider->lastCreated;

	QVERIFY  ( visibleView != nullptr );
	QCOMPARE ( visibleView->presentedPointer.to_string (), QStringLiteral ( "/object" ) );

	// The origin is carried through, not flattened -- it changes behaviour in the Form View (EDITOR-04).

	QCOMPARE ( visibleView->presentedOrigin, SelectionOrigin::GoTo );

	// The hidden views are NOT presented. Three views re-rendering a large node on every arrow key in the tree is
	// exactly what this avoids.

	QVERIFY  ( betaProvider->lastCreated  != nullptr );
	QVERIFY  ( gammaProvider->lastCreated != nullptr );
	QCOMPARE ( betaProvider->lastCreated->presentCount,  0 );
	QCOMPARE ( gammaProvider->lastCreated->presentCount, 0 );
}

void TestEditorPane::switching_tabs_presents_the_newly_visible_view ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	StubView* const betaView = betaProvider->lastCreated;

	QCOMPARE ( betaView->presentCount, 0 );

	pane->tabs ()->setCurrentIndex ( 1 );

	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "beta" ) );
	QCOMPARE ( betaView->presentCount, 1 );
	QVERIFY  ( betaView->presentedPointer.is_root () );
}

//---------------------------------------------------------------------------------------------------------------------
// Tab persistence (EDITOR-10)
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::the_active_tab_survives_a_rebuild_by_id ()
{
	// The case that makes "by id" a real distinction rather than a wording preference.
	//
	// The user is on BETA, at index 1 of [alpha, beta, gamma]. The selection moves to an array, which ALPHA cannot
	// present -- so the strip becomes [beta, gamma] and beta is now at index 0. Restoring by index would leave the user
	// on index 1, which is GAMMA: a different view, silently. Restoring by identity keeps them on beta, which is what
	// EDITOR-10 means by the tab persisting while its view remains applicable.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/object" ) ), SelectionOrigin::Tree );

	QCOMPARE ( pane->tabs ()->count (), 3 );

	pane->tabs ()->setCurrentIndex ( 1 );

	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "beta" ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/list" ) ), SelectionOrigin::Tree );

	QCOMPARE ( pane->tabs ()->count (), 2 );
	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "beta" ) );

	// The complement: when the ACTIVE view is the one that stops applying, there is nothing to preserve and the first
	// applicable tab takes over. EDITOR-10 asks for persistence only while the view remains applicable, so the pane
	// deliberately keeps no memory of a preference across that gap.

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/object" ) ), SelectionOrigin::Tree );
	pane->tabs ()->setCurrentIndex ( 0 );

	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "alpha" ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/list" ) ), SelectionOrigin::Tree );

	QCOMPARE ( pane->active_view_id (), QStringLiteral ( "beta" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Activation
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::activation_reaches_the_visible_view ()
{
	// The tree's Enter / double-click, routed to whichever view is on screen (EDITOR-04).

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	pane->activate_editing ();

	QCOMPARE ( alphaProvider->lastCreated->activateCount, 1 );
	QCOMPARE ( betaProvider->lastCreated->activateCount,  0 );
	QCOMPARE ( gammaProvider->lastCreated->activateCount, 0 );
}

QTEST_MAIN ( TestEditorPane )

#include "tst_editor_pane.moc"

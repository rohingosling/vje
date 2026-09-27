//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for EditorPane -- the pluggable view registry, the tab strip it drives, and the OPEN SET that is the
//   third term in what a tab is (EDITOR-01, EDITOR-10, VIEW-01/02/03).
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
//   The Phase 15c cases turn on one distinction that is easy to lose: OPEN is not APPLICABLE. A closed view has no tab
//   even where it applies, and an open one has no tab where it does not -- so the two terms are asserted separately and
//   in both directions rather than through the tab count alone. The persistence pair (D8) is the other: a stored EMPTY
//   set and an ABSENT one are different states, and the code that cannot tell them apart gives a user who closed every
//   tab all three back on the next launch.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/EditorPane.hpp"
#include "views/IEditorView.hpp"
#include "views/TabCloseButton.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QDir>
#include <QLabel>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

//---------------------------------------------------------------------------------------------------------------------
// A view that answers EDITOR-18's delete question and announces a change to the answer. It needs Q_OBJECT for the
// SIGNAL, which is the whole point: the pane finds the signal by name, so only a view that really declares one can
// show whether it does.
//---------------------------------------------------------------------------------------------------------------------

class DeleteSignallingView : public QLabel, public IEditorView
{
	Q_OBJECT

public:

	explicit DeleteSignallingView ( QWidget* parent )
		: QLabel ( parent )
	{
	}

	QWidget* widget () override
	{
		return this;
	}

	void present ( const JsonPointer& pointer, SelectionOrigin origin ) override
	{
		Q_UNUSED ( pointer );
		Q_UNUSED ( origin );
	}

	bool cell_delete_active () const override
	{
		return deleteActive;
	}

	bool deleteActive = false;

signals:

	void cell_delete_active_changed ();
};

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

		// The EDITOR-09 departure gate, which a close now runs (VIEW-03). Stood in for here rather than driven through
		// the real Code View, for the same reason the rest of this suite uses stubs: what is under test is the pane's
		// choreography around a refusal, not what makes a view refuse.

		bool view_deactivating () override
		{
			++deactivateCount;

			return !refusesDeparture;
		}

		// Counted rather than asserted through hasFocus(): the offscreen platform grants keyboard focus to nothing
		// (lesson Q10), so what is checkable here is that the pane ASKED the view to take it.

		void take_focus () override
		{
			++takeFocusCount;
		}

		JsonPointer     presentedPointer;
		SelectionOrigin presentedOrigin = SelectionOrigin::Programmatic;

		int presentCount    = 0;
		int activateCount   = 0;
		int deactivateCount = 0;
		int takeFocusCount  = 0;

		bool refusesDeparture = false;
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
		int     display_order () const override { return order; }

		// Non-empty, because the View menu takes its glyph from here (STYLE-06) and an empty name is exactly the
		// failure the case below exists to catch -- a menu entry silently bare while its tab carries a mark.

		QString icon_name () const override { return QStringLiteral ( "glyph-" ) + identifier; }

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

	// The provider for the view above, answering for every node so its tab exists whatever is selected.

	class DeleteSignallingProvider : public IEditorViewProvider
	{
	public:

		QString view_id       () const override { return QStringLiteral ( "delta" ); }
		QString display_name  () const override { return QStringLiteral ( "DELTA" ); }
		QString icon_name     () const override { return QStringLiteral ( "glyph-delta" ); }
		int     display_order () const override { return 0; }

		bool can_present ( const JsonNode* node ) const override
		{
			return node != nullptr;
		}

		IEditorView* create_view ( QWidget* parent ) const override
		{
			lastCreated = new DeleteSignallingView ( parent );

			return lastCreated;
		}

		mutable DeleteSignallingView* lastCreated = nullptr;
	};

	// The three stub ids, and the default open set the fixture hands the pane. Naming the default here rather than
	// reaching for config::editor::DEFAULT_OPEN_VIEWS is the point of the default being a PARAMETER: the pane is
	// exercised over views the application does not have, which is what keeps it ignorant of which views exist.

	const QString ALPHA = QStringLiteral ( "alpha" );
	const QString BETA  = QStringLiteral ( "beta" );
	const QString GAMMA = QStringLiteral ( "gamma" );

	QStringList all_three ()
	{
		return QStringList { ALPHA, BETA, GAMMA };
	}

	void register_stub_views ( EditorPane& pane )
	{
		pane.register_view ( std::make_unique<StubProvider> ( GAMMA, 20, false ) );
		pane.register_view ( std::make_unique<StubProvider> ( ALPHA,  0, true ) );
		pane.register_view ( std::make_unique<StubProvider> ( BETA,  10, false ) );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// A close-button icon whose three modes are three different colours.
	//
	// The claim under test is WHICH MODE the button asks for, and that is only observable through what it draws. Real
	// artwork cannot answer it -- Normal and Active of the same glyph differ by a shade nobody should be asserting on,
	// and Classic's Disabled is the same hue at a lower opacity by design. Three flat, unmistakable colours make the
	// mode readable from a single pixel, and they make a wrong mode a wrong COLOUR rather than a subtle one.
	//-----------------------------------------------------------------------------------------------------------------

	const QColor NORMAL_INK   ( 255,   0,   0 );
	const QColor ACTIVE_INK   (   0,   0, 255 );
	const QColor DISABLED_INK ( 128, 128, 128 );

	QIcon mode_coded_icon ( const QSize& size )
	{
		auto plate = [ &size ] ( const QColor& colour )
		{
			QPixmap pixmap ( size );

			pixmap.fill ( colour );

			return pixmap;
		};

		QIcon icon;

		icon.addPixmap ( plate ( NORMAL_INK ),   QIcon::Normal,   QIcon::Off );
		icon.addPixmap ( plate ( ACTIVE_INK ),   QIcon::Active,   QIcon::Off );
		icon.addPixmap ( plate ( DISABLED_INK ), QIcon::Disabled, QIcon::Off );

		return icon;
	}

	QColor rendered_ink ( TabCloseButton& button )
	{
		QImage canvas ( button.size (), QImage::Format_ARGB32 );

		canvas.fill ( Qt::transparent );

		// DrawChildren ALONE. QWidget::render defaults to DrawWindowBackground | DrawChildren, which fills the whole
		// rect with the palette's window brush before the widget paints -- so every pixel comes back opaque and a
		// claim about what the button did NOT ink cannot be made at all.

		button.render ( &canvas, QPoint (), QRegion (), QWidget::DrawChildren );

		return canvas.pixelColor ( button.width () / 2, button.height () / 2 );
	}

	QStringList tab_labels ( const EditorPane& pane )
	{
		QStringList labels;

		for ( int index = 0; index < pane.tabs ()->count (); ++index )
		{
			labels.append ( pane.tabs ()->tabText ( index ) );
		}

		return labels;
	}
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
	void the_tab_strip_carries_an_accessible_name ();

	// EDITOR-18: the visible view's delete state, asked and announced (Document > Delete Node's enablement).

	void a_views_delete_state_is_answered_and_its_changes_forwarded ();

	// The open set -- the third term in what a tab is (VIEW-01 / VIEW-03).

	void a_closed_view_has_no_tab_though_it_applies ();
	void an_open_view_gets_its_tab_when_it_becomes_applicable ();
	void opening_a_view_makes_it_current ();
	void view_entries_report_the_registry_in_display_order ();
	void view_entries_carry_each_view_s_own_glyph ();

	void closing_a_tab_runs_the_departure_gate ();
	void a_refused_close_changes_nothing_and_returns_the_keyboard ();
	void closing_every_tab_leaves_an_empty_pane_with_its_placeholder ();

	// Persistence (NFR-06), and the D8 distinction that is the whole of it.

	void a_first_run_takes_the_default_open_set ();
	void the_open_set_survives_a_new_pane_over_the_same_store ();
	void an_empty_stored_open_set_is_not_an_absent_one ();
	void an_id_this_build_does_not_register_is_kept ();

	// The close button on a tab (VIEW-03, STYLE-11).

	void the_close_button_is_quiet_off_the_current_tab ();
	void the_close_button_draws_its_glyph_centred ();

private:

	void load ( const char* text );

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<EditorPane>       pane;

	// Only the persistence cases need a store; the rest run the pane session-only, so their open set is exactly the
	// default they were handed.

	QTemporaryDir settingsDirectory;

	QString settings_path () const;

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

	// All three views OPEN, which is what makes every pre-15c case still a statement about applicability alone.

	pane = std::make_unique<EditorPane> ( document.get (), selection.get (), nullptr, nullptr, all_three () );

	// THREE views, registered out of display order on purpose. Three rather than two because the tab-persistence rule
	// only becomes distinguishable from an index-based one when a view that is NOT the first can survive a rebuild that
	// removes a view before it -- see the_active_tab_survives_a_rebuild_by_id.

	auto gamma = std::make_unique<StubProvider> ( GAMMA, 20, false );
	auto alpha = std::make_unique<StubProvider> ( ALPHA,  0, true );
	auto beta  = std::make_unique<StubProvider> ( BETA,  10, false );

	gammaProvider = gamma.get ();
	alphaProvider = alpha.get ();
	betaProvider  = beta.get ();

	pane->register_view ( std::move ( gamma ) );
	pane->register_view ( std::move ( alpha ) );
	pane->register_view ( std::move ( beta ) );
}

QString TestEditorPane::settings_path () const
{
	return QDir ( settingsDirectory.path () ).filePath ( QStringLiteral ( "settings.json" ) );
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

void TestEditorPane::the_tab_strip_carries_an_accessible_name ()
{
	// NFR-05, and on the BAR rather than the QTabWidget: the bar is what the keyboard lands on (NAV-04) and what the
	// arrows move within, so it is the thing that has to name itself.

	QTabBar* const tabs = pane->findChild<QTabBar*> ();

	QVERIFY ( tabs != nullptr );
	QVERIFY2 ( !tabs->accessibleName ().isEmpty (), "The editor tab strip has no accessible name" );
}

//---------------------------------------------------------------------------------------------------------------------
// The open set -- registered AND applicable AND open (VIEW-01, VIEW-03)
//
// The two directions are separate cases on purpose. A tab set derived from applicability alone passes the first of
// them the moment "closed" is implemented as anything at all; only the second says that OPEN and APPLICABLE are two
// terms rather than one, because it needs a view to be open while it does NOT apply and to acquire its tab later.
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::a_closed_view_has_no_tab_though_it_applies ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QCOMPARE ( pane->tabs ()->count (), 3 );

	QVERIFY ( pane->close_view ( BETA ) );

	// Beta presents anything, so nothing about the selection took its tab away.

	QCOMPARE ( tab_labels ( *pane ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "GAMMA" ) } ) );
	QVERIFY  ( !pane->is_view_open ( BETA ) );
	QVERIFY  ( pane->view_for_id ( BETA ) == nullptr );
}

void TestEditorPane::an_open_view_gets_its_tab_when_it_becomes_applicable ()
{
	// Alpha presents objects only. Opened while an ARRAY is selected it has no tab -- and the moment the selection
	// moves to something it can present, the tab appears with nothing else having changed.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/list" ) ), SelectionOrigin::Tree );

	QVERIFY ( pane->close_view ( ALPHA ) );

	pane->open_view ( ALPHA );

	QVERIFY  ( pane->is_view_open ( ALPHA ) );
	QCOMPARE ( tab_labels ( *pane ), QStringList ( { QStringLiteral ( "BETA" ), QStringLiteral ( "GAMMA" ) } ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/object" ) ), SelectionOrigin::Tree );

	QCOMPARE ( tab_labels ( *pane ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "BETA" ), QStringLiteral ( "GAMMA" ) } ) );
}

void TestEditorPane::opening_a_view_makes_it_current ()
{
	// The View menu's whole meaning (VIEW-02): ONE plain item per view, meaning "go to this view" -- which opens it
	// when it is closed and focuses it when it is not. The second half is why the item is not a check box: there is
	// one action here, not a state to toggle.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	StubView* const gammaView = gammaProvider->lastCreated;

	QVERIFY ( pane->close_view ( GAMMA ) );

	QSignalSpy openChanges ( pane.get (), &EditorPane::open_views_changed );

	pane->open_view ( GAMMA );

	QCOMPARE ( pane->active_view_id (), GAMMA );
	QCOMPARE ( gammaView->takeFocusCount, 1 );
	QCOMPARE ( openChanges.count (), 1 );

	// Re-opened in DISPLAY order rather than appended at the end: the strip is the registry's order, not the order the
	// user happened to open things in.

	QCOMPARE ( tab_labels ( *pane ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "BETA" ), QStringLiteral ( "GAMMA" ) } ) );

	// Already open, and the user chose it again: it becomes current and takes the keyboard, which is the whole of what
	// the menu item does in that state.

	pane->tabs ()->setCurrentIndex ( 0 );
	pane->open_view ( GAMMA );

	QCOMPARE ( pane->active_view_id (), GAMMA );
	QCOMPARE ( gammaView->takeFocusCount, 2 );

	// The open set did not change, so nothing announced that it had -- and nothing was written.

	QCOMPARE ( openChanges.count (), 1 );
}

void TestEditorPane::view_entries_report_the_registry_in_display_order ()
{
	// What the View menu is built from (VIEW-02). Every REGISTERED view, applicable or not, with its open state --
	// which is why the menu can offer a view the current selection has no tab for.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/list" ) ), SelectionOrigin::Tree );

	QVERIFY ( pane->close_view ( BETA ) );

	const QVector<EditorViewEntry> entries = pane->view_entries ();

	QCOMPARE ( entries.size (), 3 );

	QCOMPARE ( entries [ 0 ].viewId,      ALPHA );
	QCOMPARE ( entries [ 0 ].displayName, QStringLiteral ( "ALPHA" ) );
	QCOMPARE ( entries [ 1 ].viewId,      BETA );
	QCOMPARE ( entries [ 2 ].viewId,      GAMMA );

	// Alpha is open and has no tab (it cannot present an array); beta is closed. The menu says so; the strip cannot.

	QCOMPARE ( entries [ 0 ].open, true );
	QCOMPARE ( entries [ 1 ].open, false );
	QCOMPARE ( entries [ 2 ].open, true );
}

void TestEditorPane::view_entries_carry_each_view_s_own_glyph ()
{
	// The View menu's items show the SAME glyph as the view's tab (STYLE-06), and they get it from here rather than
	// from a list of their own -- so a view added later brings its own mark with it and the two surfaces cannot come
	// to disagree. What a bare name costs is a menu entry with no icon beside a tab that has one.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	const QVector<EditorViewEntry> entries = pane->view_entries ();

	QCOMPARE ( entries.size (), 3 );

	for ( const EditorViewEntry& entry : entries )
	{
		QCOMPARE ( entry.iconName, QStringLiteral ( "glyph-" ) + entry.viewId );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Closing (VIEW-03), and the EDITOR-09 gate it runs
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::closing_a_tab_runs_the_departure_gate ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QVERIFY ( pane->tabs ()->tabsClosable () );

	// Driven through the SIGNAL rather than by calling close_view, so the close button's wiring is covered too -- and
	// on a tab that is NOT current, which is the case that says the gate is asked of the view being closed rather than
	// of the active one.

	QCOMPARE ( pane->active_view_id (), ALPHA );

	StubView* const gammaView = gammaProvider->lastCreated;

	QCOMPARE ( gammaView->deactivateCount, 0 );

	QMetaObject::invokeMethod ( pane->tabs (), "tabCloseRequested", Q_ARG ( int, 2 ) );

	QCOMPARE ( gammaView->deactivateCount, 1 );
	QVERIFY  ( !pane->is_view_open ( GAMMA ) );
	QCOMPARE ( tab_labels ( *pane ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "BETA" ) } ) );

	// Closing a tab the user was not on does not move them off the one they were on.

	QCOMPARE ( pane->active_view_id (), ALPHA );

	// The view survives its tab, exactly as losing applicability leaves it intact -- so re-opening finds its state.

	QCOMPARE ( gammaProvider->lastCreated, gammaView );
}

void TestEditorPane::a_refused_close_changes_nothing_and_returns_the_keyboard ()
{
	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	StubView* const gammaView = gammaProvider->lastCreated;

	gammaView->refusesDeparture = true;

	QVERIFY ( !pane->close_view ( GAMMA ) );

	// Refusing leaves the tab where it was...

	QVERIFY  ( pane->is_view_open ( GAMMA ) );
	QCOMPARE ( pane->tabs ()->count (), 3 );

	// ...and keeping the edit means keeping the keyboard ON it, which is the same choreography a refused tab SWITCH
	// follows. Without this the user's decision to keep working is answered by putting the caret somewhere else.

	QCOMPARE ( pane->active_view_id (), GAMMA );
	QCOMPARE ( gammaView->takeFocusCount, 1 );
}

void TestEditorPane::closing_every_tab_leaves_an_empty_pane_with_its_placeholder ()
{
	// Closing the LAST tab is allowed (VIEW-03): a control the user emptied deliberately should not silently refuse
	// them, and the View menu is the way back. The same decision SET-04 already made for an empty toolbar.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	// BY NAME, not by type: a StubView is itself a QLabel (as the real views are widgets too), so the bare findChild
	// would answer with whichever view happened to be first and the case would then assert nothing about the pane.

	QLabel* const placeholder = pane->findChild<QLabel*> ( QStringLiteral ( "editorPanePlaceholder" ) );

	QVERIFY ( placeholder != nullptr );

	// isHidden() rather than isVisible(): the offscreen platform never shows a widget, so isVisible() answers false
	// for both states and would agree with a pane that never shows the placeholder at all (lesson Q10).

	QVERIFY ( placeholder->isHidden () );

	QVERIFY ( pane->close_view ( ALPHA ) );
	QVERIFY ( pane->close_view ( BETA ) );
	QVERIFY ( pane->close_view ( GAMMA ) );

	QCOMPARE ( pane->tabs ()->count (), 0 );
	QCOMPARE ( pane->active_view_id (), QString () );
	QVERIFY  ( !placeholder->isHidden () );

	// Every view is still LISTED, unchecked, so the menu can put one back.

	const QVector<EditorViewEntry> entries = pane->view_entries ();

	QCOMPARE ( entries.size (), 3 );

	for ( const EditorViewEntry& entry : entries )
	{
		QCOMPARE ( entry.open, false );
	}

	pane->open_view ( BETA );

	QCOMPARE ( pane->tabs ()->count (), 1 );
	QCOMPARE ( pane->active_view_id (), BETA );
	QVERIFY  ( placeholder->isHidden () );
}

//---------------------------------------------------------------------------------------------------------------------
// Persistence (VIEW-03, NFR-06) -- and the D8 distinction that is the whole of it
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::a_first_run_takes_the_default_open_set ()
{
	SettingsStore store ( settings_path () );

	QVERIFY ( !store.contains ( settings_keys::OPEN_VIEWS ) );

	EditorPane fresh ( document.get (), selection.get (), nullptr, &store, QStringList { ALPHA, GAMMA } );

	register_stub_views ( fresh );

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	// The default is honoured in full: beta is not in it, so it has no tab despite applying to everything.

	QCOMPARE ( tab_labels ( fresh ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "GAMMA" ) } ) );

	// A first run writes NOTHING. The key appears when the user first changes the set, which is what leaves the
	// default free to change in a later release for anyone who never touched it.

	QVERIFY ( !store.contains ( settings_keys::OPEN_VIEWS ) );
}

void TestEditorPane::the_open_set_survives_a_new_pane_over_the_same_store ()
{
	load ( SAMPLE_DOCUMENT );

	{
		SettingsStore store ( settings_path () );

		EditorPane first ( document.get (), selection.get (), nullptr, &store, all_three () );

		register_stub_views ( first );

		selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

		QVERIFY ( first.close_view ( BETA ) );
	}

	SettingsStore reopened ( settings_path () );

	EditorPane second ( document.get (), selection.get (), nullptr, &reopened, all_three () );

	register_stub_views ( second );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	// The set the user left, not the default they were first given.

	QVERIFY  ( !second.is_view_open ( BETA ) );
	QCOMPARE ( tab_labels ( second ), QStringList ( { QStringLiteral ( "ALPHA" ), QStringLiteral ( "GAMMA" ) } ) );

	// Stored in DISPLAY order rather than in the order the closes happened, so the file reads as the strip does.

	QCOMPARE ( reopened.value_string_list ( settings_keys::OPEN_VIEWS ), QStringList ( { ALPHA, GAMMA } ) );
}

void TestEditorPane::an_empty_stored_open_set_is_not_an_absent_one ()
{
	// Lesson D8, the rule Phase 10.5 established for the toolbar's layout. A reader asking isEmpty() gives a user who
	// closed every tab all of them back on the next launch -- which is the one outcome that makes the feature feel
	// broken rather than merely surprising.

	SettingsStore store ( settings_path () );

	store.set_string_list ( settings_keys::OPEN_VIEWS, QStringList () );

	QVERIFY ( store.contains ( settings_keys::OPEN_VIEWS ) );

	EditorPane emptied ( document.get (), selection.get (), nullptr, &store, all_three () );

	register_stub_views ( emptied );

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QCOMPARE ( emptied.tabs ()->count (), 0 );

	for ( const EditorViewEntry& entry : emptied.view_entries () )
	{
		QCOMPARE ( entry.open, false );
	}
}

void TestEditorPane::an_id_this_build_does_not_register_is_kept ()
{
	// A stored set naming a view this build has no provider for -- a downgrade, or a view withdrawn for a release. It
	// is carried rather than dropped, so the user's choice about it survives the round trip instead of being silently
	// decided for them by whichever build wrote last.

	SettingsStore store ( settings_path () );

	store.set_string_list ( settings_keys::OPEN_VIEWS, QStringList { ALPHA, QStringLiteral ( "delta" ) } );

	EditorPane pruned ( document.get (), selection.get (), nullptr, &store, all_three () );

	register_stub_views ( pruned );

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QCOMPARE ( tab_labels ( pruned ), QStringList ( { QStringLiteral ( "ALPHA" ) } ) );

	// An unrelated change rewrites the set; delta is still in it, at the end, behind the ids this build knows.

	pruned.open_view ( GAMMA );

	QCOMPARE
	(
		store.value_string_list ( settings_keys::OPEN_VIEWS ),
		QStringList ( { ALPHA, GAMMA, QStringLiteral ( "delta" ) } )
	);
}

//---------------------------------------------------------------------------------------------------------------------
// The tab close button (VIEW-03, STYLE-11)
//
// Read from RENDERED PIXELS rather than from a flag (lesson Q12): the claim is what the button draws, and a class that
// computed the right mode and then drew the wrong pixmap would satisfy any accessor.
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::the_close_button_is_quiet_off_the_current_tab ()
{
	// The toolkit's own rule, kept deliberately: full strength on the tab the user is on, and quiet everywhere else.
	// Three tabs each carrying a full-strength x is three things asking to be clicked.

	TabCloseButton button ( QSize ( 20, 20 ), QSize ( 16, 16 ) );

	button.set_icon ( mode_coded_icon ( QSize ( 16, 16 ) ) );

	button.set_on_current_tab ( false );

	QCOMPARE ( rendered_ink ( button ), DISABLED_INK );

	button.set_on_current_tab ( true );

	QCOMPARE ( rendered_ink ( button ), NORMAL_INK );

	// Pressed outranks both, which is what makes the button feel like a button on a tab the user is not on.

	button.setDown ( true );

	QCOMPARE ( rendered_ink ( button ), ACTIVE_INK );

	button.set_on_current_tab ( false );

	QCOMPARE ( rendered_ink ( button ), ACTIVE_INK );
}

void TestEditorPane::the_close_button_draws_its_glyph_centred ()
{
	// The button is the click target and the glyph is one of the icon set's AUTHORED sizes drawn inside it (lesson
	// Q28: a glyph scaled off its ladder is soft). So the two sizes differ, and the difference has to show up as a
	// border of untouched pixels rather than as a stretched plate.

	TabCloseButton button ( QSize ( 20, 20 ), QSize ( 16, 16 ) );

	button.set_icon ( mode_coded_icon ( QSize ( 16, 16 ) ) );
	button.set_on_current_tab ( true );

	QCOMPARE ( button.sizeHint (), QSize ( 20, 20 ) );

	QImage canvas ( button.size (), QImage::Format_ARGB32 );

	canvas.fill ( Qt::transparent );

	button.render ( &canvas, QPoint (), QRegion (), QWidget::DrawChildren );   // See rendered_ink for why the flag.

	// Centre inked, corners untouched: 20 - 16 leaves exactly two pixels of margin all round.

	QCOMPARE ( canvas.pixelColor ( 10, 10 ), NORMAL_INK );

	QCOMPARE ( canvas.pixelColor (  0,  0 ).alpha (), 0 );
	QCOMPARE ( canvas.pixelColor ( 19, 19 ).alpha (), 0 );

	// The glyph is drawn at the size it was authored at, not stretched to the button: the pixel just inside the
	// margin is inked and the one just outside it is not.

	QCOMPARE ( canvas.pixelColor (  2,  2 ), NORMAL_INK );
	QCOMPARE ( canvas.pixelColor (  1,  1 ).alpha (), 0 );
}

//---------------------------------------------------------------------------------------------------------------------
// The delete state (EDITOR-18)
//---------------------------------------------------------------------------------------------------------------------

void TestEditorPane::a_views_delete_state_is_answered_and_its_changes_forwarded ()
{
	// The pane connects a view's cell_delete_active_changed () BY NAME, as it does unsaved_view_edit_changed (), since
	// IEditorView is a plain interface and carries no signals. A misspelt lookup string compiles, raises no warning
	// and silently disconnects the window's Delete enablement -- which is the one failure this case exists for, and
	// why the view here really declares the signal rather than a stub pretending to.
	//
	// A pane of its own, because the fixture's is built over the three plain stubs and their open set.

	pane.reset ();

	pane = std::make_unique<EditorPane> ( document.get (), selection.get (), nullptr, nullptr, QStringList { QStringLiteral ( "delta" ) } );

	auto provider = std::make_unique<DeleteSignallingProvider> ();

	DeleteSignallingProvider* const deltaProvider = provider.get ();

	pane->register_view ( std::move ( provider ) );

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	DeleteSignallingView* const deltaView = deltaProvider->lastCreated;

	QVERIFY ( deltaView != nullptr );

	QSignalSpy forwarded ( pane.get (), &EditorPane::cell_delete_active_changed );

	// Answered from the VISIBLE view, both ways, so a pane that always answers one way cannot pass.

	QVERIFY ( !pane->cell_delete_active () );

	deltaView->deleteActive = true;

	emit deltaView->cell_delete_active_changed ();

	QCOMPARE ( forwarded.count (), 1 );
	QVERIFY  ( pane->cell_delete_active () );
}

QTEST_MAIN ( TestEditorPane )

#include "tst_editor_pane.moc"

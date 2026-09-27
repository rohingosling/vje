//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   EditorPane implementation -- the registry, the tab-set rebuild, and the routing of a selection to the visible view.
//   See the header for why the active tab is preserved by id rather than by index.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/EditorPane.hpp"

#include "AppConfig.hpp"
#include "services/IconLibrary.hpp"
#include "style/FocusHighlight.hpp"
#include "views/ShadedTabBar.hpp"
#include "views/TabCloseButton.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QLabel>
#include <QSize>
#include <QStringList>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	EditorPane::EditorPane
	(
		JsonDocument*      document,
		SelectionService*  selection,
		IconLibrary*       icons,
		SettingsStore*     settings,
		const QStringList& defaultOpenViewIds,
		QWidget*           parent
	)
		: QWidget ( parent )
		, document  ( document )
		, selection ( selection )
		, icons     ( icons )
		, settings  ( settings )
	{
		load_open_views ( defaultOpenViewIds );

		ShadedTabWidget* const shadedTabs = new ShadedTabWidget ( this );

		tabWidget = shadedTabs;
		tabWidget->setObjectName ( QStringLiteral ( "editorTabs" ) );

		// NFR-05, on the BAR rather than on the QTabWidget: the bar is what the keyboard lands on (NAV-04) and what
		// arrows between tabs, so it is the thing a screen reader is asked to name. Each tab already announces itself
		// through its own label.

		shadedTabs->shaded_tab_bar ()->setAccessibleName ( tr ( "Editor views" ) );
		tabWidget->setDocumentMode ( true );
		tabWidget->setIconSize ( QSize ( config::editor::TAB_ICON_SIZE, config::editor::TAB_ICON_SIZE ) );

		// VIEW-03: the tab set is the user's. Every tab closes, including the last one.

		tabWidget->setTabsClosable ( true );

		// The empty state (VIEW-03). Sized to nothing when hidden, so the strip keeps the whole pane in the ordinary
		// case and this costs a layout row only when it is showing.

		placeholder = new QLabel ( tr ( "No editor views are open.\nChoose one from the View menu." ), this );

		placeholder->setObjectName ( QStringLiteral ( "editorPanePlaceholder" ) );
		placeholder->setAlignment ( Qt::AlignCenter );
		placeholder->setWordWrap ( true );
		placeholder->setAccessibleName ( tr ( "Editor pane" ) );   // NFR-05: it is the pane's only content while shown.
		placeholder->hide ();

		QVBoxLayout* const paneLayout = new QVBoxLayout ( this );

		paneLayout->setContentsMargins ( 0, 0, 0, 0 );
		paneLayout->addWidget ( tabWidget );
		paneLayout->addWidget ( placeholder );

		// The tab strip recedes when the pane loses the keyboard (STYLE-14), scoped to the PANE -- the keyboard is
		// normally in the view BELOW the tabs, so a tab bar asking about its own focus would answer no almost always.
		//
		// The whole strip shifts rather than the active tab alone, which is what keeps the active tab one shade clear
		// of the inactive ones in both states. Dimming only the active tab would sink it to exactly the inactive shade,
		// and with three tabs that would cost the user the answer to "which view am I on?".
		//
		// The watcher is now the pane's FOCUS ORACLE as well as its palette writer: ShadedTabBar paints its four
		// surfaces from style/tab_surface.hpp, which needs the boolean rather than a palette role (see that header for
		// why Fusion's own shading had to be replaced).

		FocusHighlight* const surfaceWatcher = FocusHighlight::install ( tabWidget->tabBar (), FocusRoles::Surface, this );

		if ( surfaceWatcher != nullptr )
		{
			connect
			(
				surfaceWatcher, &FocusHighlight::focus_state_changed,
				shadedTabs->shaded_tab_bar (), &ShadedTabBar::set_pane_focused
			);

			shadedTabs->shaded_tab_bar ()->set_pane_focused ( surfaceWatcher->holds_focus () );
		}

		connect ( tabWidget, &QTabWidget::currentChanged,    this, &EditorPane::handle_tab_changed );
		connect ( tabWidget, &QTabWidget::tabCloseRequested, this, &EditorPane::handle_tab_close_requested );

		if ( selection != nullptr )
		{
			connect ( selection, &SelectionService::selection_changed, this, &EditorPane::handle_selection_changed );
			connect ( selection, &SelectionService::selection_cleared, this, &EditorPane::handle_selection_cleared );
		}

		connect ( document, &JsonDocument::reset, this, &EditorPane::handle_document_reset );

		if ( icons != nullptr )
		{
			connect ( icons, &IconLibrary::icons_changed, this, &EditorPane::handle_icons_changed );
		}
	}

	//=================================================================================================================
	// Registry
	//=================================================================================================================

	void EditorPane::register_view ( std::unique_ptr<IEditorViewProvider> provider )
	{
		RegisteredView entry;

		entry.provider = std::move ( provider );

		registeredViews.push_back ( std::move ( entry ) );

		// Stable, so display_order ties fall back to registration order -- which is what makes the tab strip's sequence
		// predictable rather than dependent on a comparator's tie-break (EDITOR-01's "stable registered order").

		std::stable_sort
		(
			registeredViews.begin (), registeredViews.end (),
			[] ( const RegisteredView& left, const RegisteredView& right )
			{
				return left.provider->display_order () < right.provider->display_order ();
			}
		);

		update_tabs ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QTabWidget* EditorPane::tabs () const
	{
		return tabWidget;
	}

	QString EditorPane::active_view_id () const
	{
		const QWidget* const activePage = tabWidget->currentWidget ();

		if ( activePage == nullptr )
		{
			return QString ();
		}

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( ( entry.view != nullptr ) && ( entry.view->widget () == activePage ) )
			{
				return entry.provider->view_id ();
			}
		}

		return QString ();
	}

	IEditorView* EditorPane::view_at_index ( int index ) const
	{
		const QWidget* const page = tabWidget->widget ( index );

		if ( page == nullptr )
		{
			return nullptr;
		}

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( ( entry.view != nullptr ) && ( entry.view->widget () == page ) )
			{
				return entry.view;
			}
		}

		return nullptr;
	}

	QVector<EditorViewEntry> EditorPane::view_entries () const
	{
		QVector<EditorViewEntry> entries;

		entries.reserve ( static_cast<int> ( registeredViews.size () ) );

		for ( const RegisteredView& entry : registeredViews )
		{
			EditorViewEntry menuEntry;

			menuEntry.viewId      = entry.provider->view_id ();
			menuEntry.displayName = entry.provider->display_name ();
			menuEntry.iconName    = entry.provider->icon_name ();
			menuEntry.open        = openViewIds.contains ( menuEntry.viewId );

			entries.append ( menuEntry );
		}

		return entries;
	}

	bool EditorPane::is_view_open ( const QString& viewId ) const
	{
		return openViewIds.contains ( viewId );
	}

	IEditorView* EditorPane::view_for_id ( const QString& viewId ) const
	{
		for ( const RegisteredView& entry : registeredViews )
		{
			const bool isLiveTab = ( entry.view != nullptr ) && ( tabWidget->indexOf ( entry.view->widget () ) >= 0 );

			if ( isLiveTab && ( entry.provider->view_id () == viewId ) )
			{
				return entry.view;
			}
		}

		return nullptr;
	}

	//=================================================================================================================
	// Commands -- the open set (VIEW-02 / VIEW-03)
	//=================================================================================================================

	void EditorPane::open_view ( const QString& viewId )
	{
		// GO TO THIS VIEW, opening it first if it is not open. One menu item therefore answers both states with one
		// label (VIEW-02), which is why an already-open view is not a no-op here.
		//
		// The focus is part of the command rather than the caller's to add afterwards: the menu item is a deliberate
		// act on a named view, which is a GESTURE in EDITOR-04's sense -- unlike present(), which stays passive. It is
		// take_focus() and not activate_editing(), so landing in a view still never opens an editor (NAV-04).

		if ( !openViewIds.contains ( viewId ) )
		{
			openViewIds.append ( viewId );

			store_open_views ();

			update_tabs ();

			emit open_views_changed ();
		}

		IEditorView* const openedView = view_for_id ( viewId );

		if ( openedView != nullptr )
		{
			tabWidget->setCurrentWidget ( openedView->widget () );

			openedView->take_focus ();
		}
	}

	bool EditorPane::close_view ( const QString& viewId )
	{
		if ( !openViewIds.contains ( viewId ) )
		{
			return true;
		}

		// EDITOR-09's departure gate, run against the view being CLOSED rather than the active one. A close button is
		// clickable on a tab that is not current, and it is that tab's uncommitted edit which is about to go out of
		// sight -- so the question has to be asked of it. For the current tab the two are the same view.
		//
		// The view itself survives a close (like losing applicability, it loses its tab and not its state), but a
		// closed tab is off screen and off the strip, so an invalid edit parked in one is exactly the silent loss
		// EDITOR-09 exists to prevent.

		IEditorView* const closingView = view_for_id ( viewId );

		if ( ( closingView != nullptr ) && !closingView->view_deactivating () )
		{
			// Keeping the edit has to mean keeping the keyboard on it -- the same choreography a refused tab SWITCH
			// already follows in handle_tab_changed.

			tabWidget->setCurrentWidget ( closingView->widget () );

			closingView->take_focus ();

			return false;
		}

		openViewIds.removeAll ( viewId );

		store_open_views ();

		update_tabs ();

		emit open_views_changed ();

		return true;
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void EditorPane::tree_node_clicked ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		if ( activeView != nullptr )
		{
			activeView->tree_node_clicked ();
		}
	}

	void EditorPane::activate_editing ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		if ( activeView != nullptr )
		{
			activeView->activate_editing ();
		}
	}

	void EditorPane::take_focus ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		// With no applicable view there is no tab and nothing to focus, so the pane itself takes the keyboard rather
		// than leaving it wherever it was -- Tab must always land somewhere or the cycle silently stalls.

		if ( activeView != nullptr )
		{
			activeView->take_focus ();
		}
		else
		{
			setFocus ( Qt::TabFocusReason );
		}
	}

	bool EditorPane::cell_cut ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_cut ();
	}

	bool EditorPane::cell_copy ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_copy ();
	}

	bool EditorPane::cell_paste ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_paste ();
	}

	bool EditorPane::cell_delete ()
	{
		IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_delete ();
	}

	bool EditorPane::cell_delete_active () const
	{
		const IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_delete_active ();
	}

	bool EditorPane::cell_clipboard_active () const
	{
		const IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->cell_clipboard_active ();
	}

	PrintContent EditorPane::print_content ( int availableColumns ) const
	{
		const IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) ? activeView->print_content ( availableColumns ) : PrintContent ();
	}

	bool EditorPane::has_unsaved_view_edit () const
	{
		// Every LIVE view, not the active one: a background Code tab holding an edit is unsaved work the title has to
		// report. `views` holds only the ones with a tab, which is exactly the set that can be holding anything --
		// EditorPane destroys a view with its tab (15c), so a closed one has already run the EDITOR-09 gate.

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( ( entry.view != nullptr ) && entry.view->has_unsaved_view_edit () )
			{
				return true;
			}
		}

		return false;
	}

	bool EditorPane::active_view_claims_tab_key () const
	{
		// The TAB STRIP never claims Tab, whatever view is behind it. A click on a tab now leaves the keyboard on the
		// strip (so the arrow keys move between tabs), and the Code View answers claims_tab_key() with true for its
		// indentation -- so without this, Tab would stop cycling the workspace the moment a user clicked the Code tab,
		// which is precisely when they are most likely to want to get back out (NAV-04, EDITOR-07).

		if ( ( tabWidget->tabBar () != nullptr ) && tabWidget->tabBar ()->hasFocus () )
		{
			return false;
		}

		const IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView != nullptr ) && activeView->claims_tab_key ();
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void EditorPane::handle_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		pendingPointer = pointer;
		pendingOrigin  = origin;
		hasPending     = true;

		update_tabs ();

		present_current ();
	}

	void EditorPane::handle_selection_cleared ()
	{
		pendingPointer = JsonPointer ();
		pendingOrigin  = SelectionOrigin::Programmatic;
		hasPending     = false;

		update_tabs ();
	}

	void EditorPane::handle_document_reset ()
	{
		// The document was replaced under whatever was selected. The tree re-selects and that arrives separately, but
		// the applicable tab set may have changed already (an empty document offers none), so settle it now.

		update_tabs ();

		present_current ();
	}

	void EditorPane::handle_icons_changed ()
	{
		apply_tab_icons ();
	}

	void EditorPane::handle_tab_changed ( int index )
	{
		if ( rebuildingTabs || revertingTab )
		{
			// A rebuild's tab churn is not the user leaving a view, and the revert below is this method's own doing.
			// Either way the departure gate must not run, but the baseline still has to follow the strip.

			previousTabIndex = index;

			return;
		}

		// EDITOR-09's departure gate. currentChanged fires AFTER the switch, so "aborting" it means putting the index
		// back -- done synchronously, inside the same slot, so no paint happens in between and the user sees the tab
		// they refused to leave simply not change.
		//
		// The view being LEFT is the one at the previous index; by the time this runs, active_view_id() already names
		// the new one, which is why the index is tracked rather than read back.

		IEditorView* const leavingView = view_at_index ( previousTabIndex );

		if ( ( leavingView != nullptr ) && !leavingView->view_deactivating () )
		{
			revertingTab = true;

			tabWidget->setCurrentIndex ( previousTabIndex );

			revertingTab = false;

			// "Keep editing" has to mean the keyboard goes BACK to the edit being kept. The refusal arrived from a tab
			// CLICK, which has just taken focus to the strip -- leaving it there would answer the user's decision to
			// keep working by putting the caret somewhere they cannot type.

			leavingView->take_focus ();

			return;
		}

		previousTabIndex = index;

		// The close buttons answer to which tab is current (see TabCloseButton), so they follow the switch.

		update_tab_close_button_states ();

		// A view is presented when it BECOMES visible rather than on every selection change, so a hidden view never
		// re-renders a large node behind the user's back.

		IEditorView* const activeView = view_for_id ( active_view_id () );

		if ( activeView != nullptr )
		{
			activeView->view_activated ();
		}

		present_current ();
	}

	void EditorPane::handle_tab_close_requested ( int index )
	{
		const IEditorView* const closingView = view_at_index ( index );

		if ( closingView == nullptr )
		{
			return;
		}

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( entry.view == closingView )
			{
				close_view ( entry.provider->view_id () );

				return;
			}
		}
	}

	//=================================================================================================================
	// Commands -- the EDITOR-09 gate for callers other than the tab strip.
	//=================================================================================================================

	bool EditorPane::confirm_leaving_active_view ()
	{
		// The same gate the tab strip runs, for the File actions EDITOR-09 also names (New / Open / Close / Save /
		// Exit). Those arrive in Phase 10 with the exception of Exit, which MainWindow already routes here.

		IEditorView* const activeView = view_for_id ( active_view_id () );

		return ( activeView == nullptr ) || activeView->view_deactivating ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void EditorPane::update_tabs ()
	{
		const JsonNode* const node = hasPending ? document->resolve ( pendingPointer ) : nullptr;

		// THE RULE, in one place: registered AND applicable AND open, in registered order (EDITOR-01, VIEW-01/03).

		QStringList desiredIds;

		for ( const RegisteredView& entry : registeredViews )
		{
			const QString viewId = entry.provider->view_id ();

			if ( entry.provider->can_present ( node ) && openViewIds.contains ( viewId ) )
			{
				desiredIds.append ( viewId );
			}
		}

		// Before the early return below: the empty strip has two causes and only one of them has an answer, and which
		// one holds can change (a document loads) without the tab set changing at all.

		update_placeholder ();

		QStringList currentIds;

		for ( int index = 0; index < tabWidget->count (); ++index )
		{
			const QWidget* const page = tabWidget->widget ( index );

			for ( const RegisteredView& entry : registeredViews )
			{
				if ( ( entry.view != nullptr ) && ( entry.view->widget () == page ) )
				{
					currentIds.append ( entry.provider->view_id () );

					break;
				}
			}
		}

		if ( desiredIds == currentIds )
		{
			return;
		}

		// EDITOR-10: the active tab survives a rebuild. By ID -- an index would silently land on a different view the
		// moment a tab appeared or vanished, which is exactly when a rebuild happens.

		const QString previousActiveId = active_view_id ();

		rebuildingTabs = true;

		// Detach every page and keep it alive, parented here. Views are built once and reused: a view that becomes
		// inapplicable loses its tab, not its state, so returning to a node it can present finds it as it was.

		while ( tabWidget->count () > 0 )
		{
			QWidget* const page = tabWidget->widget ( 0 );

			tabWidget->removeTab ( 0 );

			page->setParent ( this );
			page->hide ();
		}

		// Driven by desiredIds rather than by re-asking can_present, so the rule is stated ONCE. Re-deriving it here was
		// how the tab set and the set it was compared against came to disagree the moment the rule grew its third term
		// (VIEW-03): the strip was rebuilt from applicability alone while the decision to rebuild had already taken the
		// open set into account, so a closed view's tab came straight back.

		for ( RegisteredView& entry : registeredViews )
		{
			if ( !desiredIds.contains ( entry.provider->view_id () ) )
			{
				continue;
			}

			if ( entry.view == nullptr )
			{
				entry.view = entry.provider->create_view ( this );

				// VIEW-04. Connected by QOBJECT LOOKUP rather than through IEditorView, which is a plain interface and
				// carries no signals -- the alternative is a callback on every provider, which is what 15d needed for
				// the JSONPath results and what this deliberately does not repeat: only one view can hold unsaved work,
				// and a view that raises nothing simply never reaches here.

				if ( QObject* const viewObject = dynamic_cast<QObject*> ( entry.view ) )
				{
					const QMetaObject* const meta = viewObject->metaObject ();

					if ( meta->indexOfSignal ( "unsaved_view_edit_changed()" ) >= 0 )
					{
						connect
						(
							viewObject, SIGNAL ( unsaved_view_edit_changed () ),
							this,       SIGNAL ( unsaved_view_edit_changed () )
						);
					}

					// EDITOR-18's Delete enablement, found the same way and for the same reason.

					if ( meta->indexOfSignal ( "cell_delete_active_changed()" ) >= 0 )
					{
						connect
						(
							viewObject, SIGNAL ( cell_delete_active_changed () ),
							this,       SIGNAL ( cell_delete_active_changed () )
						);
					}
				}
			}

			tabWidget->addTab ( entry.view->widget (), entry.provider->display_name () );
		}

		apply_tab_icons ();

		const int restoredIndex = desiredIds.indexOf ( previousActiveId );

		tabWidget->setCurrentIndex ( ( restoredIndex >= 0 ) ? restoredIndex : 0 );

		// Set explicitly rather than left to the handler above: setCurrentIndex emits nothing when the index is
		// unchanged, which would leave the departure gate pointing at whichever tab happened to be there before the
		// rebuild -- and after a rebuild that is very often a different view.

		previousTabIndex = tabWidget->currentIndex ();

		// AFTER the current index is settled, and it has to be here rather than left to handle_tab_changed: that slot
		// returns early while rebuildingTabs is set, so the setCurrentIndex above announces nothing. Without this the
		// close buttons carry the current-tab flag they were installed with, which is the flag from BEFORE the rebuild.

		update_tab_close_button_states ();

		rebuildingTabs = false;
	}

	void EditorPane::present_current ()
	{
		if ( rebuildingTabs || !hasPending )
		{
			return;
		}

		IEditorView* const activeView = view_for_id ( active_view_id () );

		if ( activeView != nullptr )
		{
			activeView->present ( pendingPointer, pendingOrigin );
		}
	}

	void EditorPane::load_open_views ( const QStringList& defaultOpenViewIds )
	{
		// AN EMPTY STORED LIST IS NOT AN ABSENT ONE (lesson D8, the rule Phase 10.5 established for the toolbar's
		// layout). A user who closed every tab must come back to an empty pane; only a store that has never been
		// written gets the defaults. Hence contains(), never isEmpty().

		const bool hasStoredSet = ( settings != nullptr ) && settings->contains ( settings_keys::OPEN_VIEWS );

		openViewIds = hasStoredSet ? settings->value_string_list ( settings_keys::OPEN_VIEWS ) : defaultOpenViewIds;

		openViewIds.removeDuplicates ();
	}

	void EditorPane::store_open_views ()
	{
		// Kept in DISPLAY order, with any id this build does not register at the end. Normalizing on the way OUT rather
		// than on the way in is what lets an unregistered id survive: it is never compared against the registry, only
		// carried.
		//
		// The ordering happens whether or not there is a store, so the set is the same value in a session-only pane as
		// in a persisted one -- a function whose effect depended on having somewhere to write would be a difference
		// between the tested configuration and the shipped one.

		QStringList ordered;

		for ( const RegisteredView& entry : registeredViews )
		{
			const QString viewId = entry.provider->view_id ();

			if ( openViewIds.contains ( viewId ) )
			{
				ordered.append ( viewId );
			}
		}

		for ( const QString& viewId : std::as_const ( openViewIds ) )
		{
			if ( !ordered.contains ( viewId ) )
			{
				ordered.append ( viewId );
			}
		}

		openViewIds = ordered;

		if ( settings != nullptr )
		{
			settings->set_string_list ( settings_keys::OPEN_VIEWS, openViewIds );
		}
	}

	void EditorPane::update_placeholder ()
	{
		// The placeholder answers exactly one question -- "where did my tabs go?" -- and that question only exists
		// when the answer is "you closed them". Both terms below are needed and neither is the tab count:
		//
		//   - NO REGISTERED VIEW IS OPEN, rather than "the strip is empty". A strip left empty because the open views
		//     do not apply to this node is the pre-existing state, and telling that user nothing is open would be
		//     false.
		//   - SOME VIEW WOULD APPLY. With no document nothing does, and an instruction to open a view would be advice
		//     that changes nothing on screen -- so an empty document keeps the blank pane it has always had.
		//
		// The first implies the strip is empty, so the count never has to be consulted -- which also makes this safe
		// to call BEFORE the rebuild, where the count is still the previous one.

		const JsonNode* const node = hasPending ? document->resolve ( pendingPointer ) : nullptr;

		bool anyViewApplies = false;
		bool anyViewIsOpen  = false;

		for ( const RegisteredView& entry : registeredViews )
		{
			anyViewApplies = anyViewApplies || entry.provider->can_present ( node );
			anyViewIsOpen  = anyViewIsOpen  || openViewIds.contains ( entry.provider->view_id () );
		}

		placeholder->setVisible ( !anyViewIsOpen && anyViewApplies );
	}

	void EditorPane::apply_tab_icons ()
	{
		if ( icons == nullptr )
		{
			return;
		}

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( entry.view == nullptr )
			{
				continue;
			}

			const int tabIndex = tabWidget->indexOf ( entry.view->widget () );

			if ( tabIndex < 0 )
			{
				continue;
			}

			const QString iconName = entry.provider->icon_name ();

			// A name the library does not hold leaves the tab with its label alone rather than a blank square -- which
			// is what lets the view glyphs be added later without the tabs looking broken until then (STYLE-06).

			if ( !iconName.isEmpty () && icons->has_icon ( iconName ) )
			{
				tabWidget->setTabIcon ( tabIndex, icons->icon ( iconName ) );
			}
		}

		// The close buttons re-tint from the same call, so a theme change or a change of Interface style moves the tab
		// chrome with everything else -- which is the whole reason the toolkit's baked-in glyph was replaced.

		apply_tab_close_buttons ();
	}

	void EditorPane::apply_tab_close_buttons ()
	{
		QTabBar* const bar = tabWidget->tabBar ();

		if ( ( bar == nullptr ) || ( icons == nullptr ) || !icons->has_icon ( icon_names::TAB_CLOSE ) )
		{
			// With no library the toolkit's own close button stays, which is what the headless tests see: they assert
			// the CLOSING behaviour, and that is the tab bar's signal rather than any particular widget on the tab.

			return;
		}

		const QIcon closeIcon = icons->icon ( icon_names::TAB_CLOSE );

		for ( int index = 0; index < bar->count (); ++index )
		{
			TabCloseButton* button = qobject_cast<TabCloseButton*> ( bar->tabButton ( index, QTabBar::RightSide ) );

			if ( button == nullptr )
			{
				// The button QTabBar built for setTabsClosable is replaced rather than suppressed, so the tab's width
				// reservation and the SE_TabBarTabRightButton placement both stay exactly as they were. setTabButton
				// deletes the one it displaces.

				const int buttonExtent = style ()->pixelMetric ( QStyle::PM_TabCloseIndicatorWidth, nullptr, bar );

				button = new TabCloseButton
				(
					QSize ( buttonExtent, buttonExtent ),
					QSize ( config::editor::TAB_ICON_SIZE, config::editor::TAB_ICON_SIZE ),
					bar
				);

				// By the WIDGET rather than by the index it currently sits at: update_tabs rebuilds the strip, so an
				// index captured here names a different view a moment later. This is EDITOR-10's rule, one layer down.

				connect
				(
					button, &QAbstractButton::clicked,
					this,
					[ this, button ] ()
					{
						QTabBar* const tabs = tabWidget->tabBar ();

						for ( int position = 0; position < tabs->count (); ++position )
						{
							if ( tabs->tabButton ( position, QTabBar::RightSide ) == button )
							{
								handle_tab_close_requested ( position );

								return;
							}
						}
					}
				);

				bar->setTabButton ( index, QTabBar::RightSide, button );
			}

			button->set_icon ( closeIcon );
			button->set_on_current_tab ( index == bar->currentIndex () );

			// NFR-05: the button is the one control on a tab, and "Close" alone would announce three identical
			// commands. The tab's own label is what tells them apart.

			button->setAccessibleName ( tr ( "Close %1 view" ).arg ( bar->tabText ( index ) ) );
			button->setToolTip ( tr ( "Close %1 view" ).arg ( bar->tabText ( index ) ) );
		}
	}

	void EditorPane::update_tab_close_button_states ()
	{
		QTabBar* const bar = tabWidget->tabBar ();

		if ( bar == nullptr )
		{
			return;
		}

		for ( int index = 0; index < bar->count (); ++index )
		{
			TabCloseButton* const button = qobject_cast<TabCloseButton*> ( bar->tabButton ( index, QTabBar::RightSide ) );

			if ( button != nullptr )
			{
				button->set_on_current_tab ( index == bar->currentIndex () );
			}
		}
	}
}

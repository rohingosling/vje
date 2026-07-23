//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QSize>
#include <QStringList>
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
		JsonDocument*     document,
		SelectionService* selection,
		IconLibrary*      icons,
		QWidget*          parent
	)
		: QWidget ( parent )
		, document  ( document )
		, selection ( selection )
		, icons     ( icons )
	{
		ShadedTabWidget* const shadedTabs = new ShadedTabWidget ( this );

		tabWidget = shadedTabs;
		tabWidget->setObjectName ( QStringLiteral ( "editorTabs" ) );
		tabWidget->setDocumentMode ( true );
		tabWidget->setIconSize ( QSize ( config::editor::TAB_ICON_SIZE, config::editor::TAB_ICON_SIZE ) );

		QVBoxLayout* const paneLayout = new QVBoxLayout ( this );

		paneLayout->setContentsMargins ( 0, 0, 0, 0 );
		paneLayout->addWidget ( tabWidget );

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

		connect ( tabWidget, &QTabWidget::currentChanged, this, &EditorPane::handle_tab_changed );

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

		// A view is presented when it BECOMES visible rather than on every selection change, so a hidden view never
		// re-renders a large node behind the user's back.

		IEditorView* const activeView = view_for_id ( active_view_id () );

		if ( activeView != nullptr )
		{
			activeView->view_activated ();
		}

		present_current ();
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

		// Which views apply, in registered order (EDITOR-01).

		QStringList desiredIds;

		for ( const RegisteredView& entry : registeredViews )
		{
			if ( entry.provider->can_present ( node ) )
			{
				desiredIds.append ( entry.provider->view_id () );
			}
		}

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

		for ( RegisteredView& entry : registeredViews )
		{
			if ( !entry.provider->can_present ( node ) )
			{
				continue;
			}

			if ( entry.view == nullptr )
			{
				entry.view = entry.provider->create_view ( this );
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
	}
}

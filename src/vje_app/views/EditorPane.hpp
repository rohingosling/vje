//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   EditorPane -- the right half of the workspace: a QTabWidget whose tabs are the registered editor
//   views applicable to the current selection (EDITOR-01).
//
//   WHAT IT OWNS
//
//     - The view REGISTRY. Providers are registered once at composition and kept sorted by display order, so the tab
//       strip's sequence is a property of the registration rather than of whatever happened to be applicable first.
//     - The tab set, rebuilt when the applicable-provider set changes and only then. A rebuild preserves the active tab
//       BY VIEW ID (EDITOR-10) -- an index would silently point at a different view once a tab appeared or vanished.
//     - Routing the selection to the visible view, and only to it. A hidden view is presented lazily when its tab is
//       shown, which is what stops three views re-rendering a large node on every arrow key in the tree.
//
//   WHAT IT DOES NOT. It knows nothing about forms, tables, or JSON text -- only the contract in IEditorView.hpp. That
//   is the point of the seam: a fourth view is one provider class and one register_view() call.
//
//   Version 2.0 registers Form (Phase 7), then Text and Code (Phase 8).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "services/SelectionService.hpp"
#include "views/IEditorView.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

class QTabWidget;

namespace vje
{
	class IconLibrary;
	class JsonDocument;

	//*****************************************************************************************************************
	// Class: EditorPane
	//*****************************************************************************************************************

	class EditorPane : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The icon library may be null, in which case tabs show their labels alone -- the form the headless tests use.

		EditorPane
		(
			JsonDocument*     document,
			SelectionService* selection,
			IconLibrary*      icons,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// Registry
		//=============================================================================================================

	public:

		// Register a view kind. Ownership of the provider passes to the pane. Registration order breaks ties in
		// display_order; call before the first selection arrives.

		void register_view ( std::unique_ptr<IEditorViewProvider> provider );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QTabWidget* tabs () const;

		// The view id of the visible tab; empty when there is none. This is the identity EDITOR-10 preserves.

		QString active_view_id () const;

		// The live view for a registered id, or nullptr when that view has no tab at the moment.

		IEditorView* view_for_id ( const QString& viewId ) const;

		// The live view behind a tab index, or nullptr. Needed by the EDITOR-09 gate, which has to name the view being
		// LEFT -- and by the time QTabWidget says the current tab changed, "current" already means the new one.

		IEditorView* view_at_index ( int index ) const;

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public slots:

		// The tree's two gesture channels, forwarded to the VISIBLE view only -- a view in a background tab has no
		// business taking the caret (EDITOR-04). Each is the corresponding IEditorView method; see it for the split.

		void tree_node_clicked ();
		void activate_editing  ();

		// Hand the keyboard to the visible view (NAV-04). Focusing is not editing -- see IEditorView::take_focus.

		void take_focus ();

		//=============================================================================================================
		// Commands -- the EDITOR-09 departure gate
		//=============================================================================================================

	public:

		// May the application leave the active view? A view holding an uncommitted edit auto-commits it when valid and
		// asks keep / discard when it is not; answering false means the user chose to keep editing, and the caller must
		// ABORT whatever it was about to do.
		//
		// The tab strip runs this gate itself. This is the entry point for the other departures EDITOR-09 names -- the
		// File actions -- of which only Exit is live before Phase 10.

		bool confirm_leaving_active_view ();

		//=============================================================================================================
		// Value Accessors -- keyboard policy
		//=============================================================================================================

	public:

		// Whether the visible view wants the Tab key for itself, which takes it out of the NAV-04 focus cycle. No
		// Phase 7 view does; the Code View will (EDITOR-07).

		bool active_view_claims_tab_key () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin );
		void handle_selection_cleared ();

		void handle_document_reset ();
		void handle_icons_changed  ();
		void handle_tab_changed    ( int index );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// One registered view kind and its live instance. The instance exists only while its tab does; a provider whose
		// can_present() turns false has its view destroyed with the tab.

		struct RegisteredView
		{
			std::unique_ptr<IEditorViewProvider> provider;

			IEditorView* view = nullptr;   // Non-owning: owned by the tab widget through Qt parenting.
		};

		void update_tabs    ();            // Rebuild the strip when the applicable set changed; preserve the active id.
		void present_current ();           // Route the pending selection to the visible view.

		void apply_tab_icons ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		IconLibrary*      icons;

		//=============================================================================================================
		// Data Members -- widgets and registry.
		//=============================================================================================================

	private:

		QTabWidget* tabWidget = nullptr;

		std::vector<RegisteredView> registeredViews;   // Sorted by display order.

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		JsonPointer     pendingPointer;
		SelectionOrigin pendingOrigin    = SelectionOrigin::Programmatic;
		bool            hasPending       = false;

		// Set while update_tabs() is adding and removing tabs, so the currentChanged storm that causes does not present
		// against a half-built strip.

		bool rebuildingTabs = false;

		// Which tab was current before the last change -- i.e. which view is being LEFT. Tracked rather than read back,
		// because QTabWidget::currentChanged fires after the switch, at which point "current" is the destination.

		int previousTabIndex = -1;

		// Set while handle_tab_changed() is putting a refused switch back, so its own currentChanged does not re-enter
		// the gate and ask the user the same question a second time.

		bool revertingTab = false;
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   EditorPane -- the right half of the workspace: a QTabWidget whose tabs are the registered editor
//   views that are applicable to the current selection AND open (EDITOR-01, VIEW-01/03).
//
//   ONE RULE, THREE TERMS. A tab exists when its view is REGISTERED, APPLICABLE and OPEN. Phase 15c added the third
//   term and nothing else: the registered order, EDITOR-10's restore-by-id, and the lazy presentation of a hidden view
//   are all untouched, and there is deliberately no second notion of what a tab is anywhere in this class.
//
//   WHAT IT OWNS
//
//     - The view REGISTRY. Providers are registered once at composition and kept sorted by display order, so the tab
//       strip's sequence is a property of the registration rather than of whatever happened to be applicable first.
//     - The OPEN SET (VIEW-03) and its persistence. The user opens and closes tabs; the set they leave is the set they
//       come back to. Seeded from a default list passed in at construction, and stored under workspace.openViews.
//     - The tab set, rebuilt when the registered-and-applicable-and-open set changes and only then. A rebuild
//       preserves the active tab BY VIEW ID (EDITOR-10) -- an index would silently point at a different view once a
//       tab appeared or vanished.
//     - Routing the selection to the visible view, and only to it. A hidden view is presented lazily when its tab is
//       shown, which is what stops three views re-rendering a large node on every arrow key in the tree.
//
//   WHAT IT DOES NOT. It knows nothing about forms, tables, or JSON text -- only the contract in IEditorView.hpp. That
//   is the point of the seam: a fourth view is one provider class and one register_view() call. It equally does not
//   know WHICH views exist by name: the default open set arrives as a parameter, so AppConfig owns that list and this
//   class owns the mechanism.
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
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QTabWidget;

namespace vje
{
	class IconLibrary;
	class JsonDocument;
	class SettingsStore;

	//*****************************************************************************************************************
	// Struct: EditorViewEntry
	//*****************************************************************************************************************
	//
	// One registered view, as the View menu needs to see it (VIEW-02). The menu is built from the registry rather than
	// hand-listed, so a view added later appears in it without the menu being edited -- the property the tab strip has
	// always had, extended to the menu.
	//
	//*****************************************************************************************************************

	struct EditorViewEntry
	{
		QString viewId;
		QString displayName;

		// The provider's IconLibrary name (STYLE-06) -- the same glyph the view's tab carries, so the menu entry and
		// the tab are the same command wearing the same mark. May be empty, and may name a glyph the library does not
		// hold; the caller leaves the entry bare in either case rather than showing a blank square.

		QString iconName;

		bool open = false;
	};

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
		// The settings store may be null too, and the open set is then session-only.
		//
		// defaultOpenViewIds is what a FIRST RUN opens -- the seed used only when the store holds no open set at all.
		// It is passed in rather than read from AppConfig here so this class never names a view (see the header note).

		EditorPane
		(
			JsonDocument*      document,
			SelectionService*  selection,
			IconLibrary*       icons,
			SettingsStore*     settings           = nullptr,
			const QStringList& defaultOpenViewIds = QStringList (),
			QWidget*           parent             = nullptr
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
		// Value Accessors -- the open set (VIEW-02 / VIEW-03)
		//=============================================================================================================

	public:

		// Every registered view in display order, each saying whether it is open. This is what the View menu renders,
		// and asking the pane rather than keeping a parallel list is what keeps the menu and the strip one truth.

		QVector<EditorViewEntry> view_entries () const;

		// Is this view open? Open is independent of APPLICABLE: a view can be open and have no tab, which is exactly
		// what makes it reappear the moment the selection becomes one it can present.

		bool is_view_open ( const QString& viewId ) const;

		//=============================================================================================================
		// Commands -- the open set (VIEW-02 / VIEW-03)
		//=============================================================================================================

	public:

		// GO TO a view, opening it first if it is not already open: it becomes the current tab and takes the keyboard.
		// One command for both states, which is what lets the View menu carry one plain item per view rather than a
		// check box the user has to read before clicking (VIEW-02). Focusing is not editing -- see take_focus.

		void open_view ( const QString& viewId );

		// Close a view. Runs the EDITOR-09 departure gate first and answers false when the user chose to keep editing,
		// in which case nothing changed at all. Closing the LAST view is allowed: the pane shows its placeholder and
		// the View menu re-opens whatever the user wants (VIEW-03).

		bool close_view ( const QString& viewId );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The open set changed, by any route -- a close button, the View menu, or the stored set being applied. It is
		// emitted for the CHANGE rather than by whatever caused it, so a listener cannot be reached by one route and
		// missed by another.
		//
		// Its consumer is MainWindow's Copy JSONPath Result enablement (QUERY-08): closing that tab destroys the view
		// and the results it held, and a view being destroyed cannot report that itself. (The View menu's entries do
		// not listen -- they are plain "go to this view" items with no state to mirror, VIEW-02.)

		void open_views_changed ();

		// VIEW-04. Raised when has_unsaved_view_edit () may have changed -- on the keystroke that first differs from
		// the committed text, and again on the commit or discard that closes the gap.

		void unsaved_view_edit_changed ();

		// cell_delete_active () may have changed -- forwarded from any view that raises cell_delete_active_changed (),
		// which today is the Form View's array table as a row or column is selected or deselected. Nothing else the
		// window's enablement listens to moves when that happens.

		void cell_delete_active_changed ();

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

		// The cell clipboard (EDITOR-11), forwarded to the visible view. Each returns true when the view handled it, so
		// MainWindow can fall back to the node clipboard. cell_clipboard_active() drives the Edit-menu enablement.

		bool cell_cut   ();
		bool cell_copy  ();
		bool cell_paste ();

		// EDITOR-18. Answers true only when the active view has a row or column selected to remove.

		bool cell_delete ();

		// Would cell_delete act right now? Drives Document > Delete Node's enablement while the pane holds the keyboard.

		bool cell_delete_active () const;

		bool cell_clipboard_active () const;

		//=============================================================================================================
		// Value Accessors -- printing (FILE-12)
		//=============================================================================================================

	public:

		// What the VISIBLE view would put on paper, rendered for a page availableColumns characters wide (see
		// IEditorView::print_content). Empty when there is no tab at all -- which is what File > Print reports rather
		// than sending a blank page to the printer.

		PrintContent print_content ( int availableColumns ) const;

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

		// VIEW-04: does ANY open view hold an edit the document does not have? Any, not the active one -- a Code View
		// edit is unsaved work while its tab sits in the background just as much as while it is on screen, and the
		// title has to say so either way.

		bool has_unsaved_view_edit () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin );
		void handle_selection_cleared ();

		void handle_document_reset ();
		void handle_icons_changed  ();
		void handle_tab_changed    ( int index );

		void handle_tab_close_requested ( int index );

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

		void update_tabs    ();            // Rebuild the strip when the tab set changed; preserve the active id.
		void present_current ();           // Route the pending selection to the visible view.

		void apply_tab_icons ();

		// The tab close buttons (VIEW-03, STYLE-11). Installed and re-tinted by the first; the second is only the
		// current-tab flag, which changes far more often than the strip does.

		void apply_tab_close_buttons        ();
		void update_tab_close_button_states ();

		// The open set's persistence, and the placeholder that answers "where did my tabs go?".

		void load_open_views ( const QStringList& defaultOpenViewIds );
		void store_open_views ();

		void update_placeholder ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		IconLibrary*      icons;
		SettingsStore*    settings;

		//=============================================================================================================
		// Data Members -- widgets and registry.
		//=============================================================================================================

	private:

		QTabWidget* tabWidget = nullptr;

		// Shown in place of the strip when the user has closed every tab (VIEW-03). Deliberately NOT shown for the
		// other empty state -- no document, so no view applies -- where a blank pane is the honest answer and an
		// instruction to open a view would be advice that does nothing.

		QLabel* placeholder = nullptr;

		std::vector<RegisteredView> registeredViews;   // Sorted by display order.

		// The view ids the user has open (VIEW-03). Ids this build does not register are KEPT rather than dropped, so
		// a downgrade, or a view removed for a release, does not silently discard the user's choice about it.

		QStringList openViewIds;

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

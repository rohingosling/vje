//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TreeViewPane -- the left half of the workspace: a QTreeView over JsonTreeModel, plus the
//   behaviour that turns a generic tree widget into VJE's navigation pane.
//
//   WHAT IT OWNS
//
//     - Selection sync with SelectionService in BOTH directions (TREE-04, NAV-01). Outbound, a tree selection is written
//       with SelectionOrigin::Tree. Inbound, a revealing origin (reveals_selection(): Go To, Find, paste, drill-in)
//       expands whatever it takes to show the node; a non-revealing one moves the highlight only if the row is already
//       on show, and otherwise leaves the tree exactly as it is (EDITOR-04). The two directions are guarded against
//       each other; a selection service that re-signals what the tree just wrote must not bounce back.
//     - Expansion: individual, whole-tree (TREE-05), and per-subtree from the context menu.
//     - The node context menu (TREE-06), built from command actions MainWindow injects so enablement stays in one place.
//     - Restoring expansion and selection by POINTER across a projection rebuild (TREE-07, NAV-03) --
//       the reason a Code View commit keeps the user where they were instead of dumping them at the root.
//
//   WHAT IT DOES NOT. The node commands themselves (cut / copy / paste / duplicate / delete / rename / move / add) are
//   Phase 9. This pane hosts and scopes their actions; it does not implement them.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "services/SelectionService.hpp"
#include "views/NodeContextActions.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QList>
#include <QPoint>
#include <QWidget>

class QAction;
class QModelIndex;
class QTreeView;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The four expand / collapse commands the tree offers, in both the View menu and its own context menu (TREE-05 /
	// TREE-06). They are MainWindow's shared QActions, so the two surfaces carry one label, one icon, and one enabled
	// state between them.
	//
	// DELIBERATELY NOT PART OF NodeContextActions. That set is shared with the Form View's key-label menu, and these
	// four are the tree's alone -- a pane with no tree offering "Collapse Subtree" would raise the question of what it
	// could possibly act on.
	//-----------------------------------------------------------------------------------------------------------------

	struct TreeViewCommands
	{
		QAction* expandAll       = nullptr;
		QAction* collapseAll     = nullptr;
		QAction* expandSubtree   = nullptr;
		QAction* collapseSubtree = nullptr;
	};

	class IconLibrary;
	class JsonDocument;
	class JsonTreeModel;
	class PaneHeader;

	//*****************************************************************************************************************
	// Class: TreeViewPane
	//*****************************************************************************************************************

	class TreeViewPane : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		TreeViewPane
		(
			JsonDocument*     document,
			SelectionService* selection,
			IconLibrary*      icons,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QTreeView*     view   () const;
		JsonTreeModel* model  () const;
		PaneHeader*    header () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Inject the shared command actions the context menu presents. Also scopes the node-operation SHORTCUTS to this
		// widget (Qt::WidgetWithChildrenShortcut), so Delete / F2 / Ctrl+D act on the tree without hijacking those keys
		// from a text editor elsewhere in the window.

		void set_context_actions ( const NodeContextActions& actions );

		// Inject the four expand / collapse commands the context menu appends below the node commands. Null members are
		// simply not offered, which is the form the headless tests use.

		void set_view_commands ( const TreeViewCommands& commands );

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public slots:

		// Hand the keyboard to the tree (NAV-04). Focusing is not selecting: whatever row was current stays current.

		void take_focus ();

	public:

		// The context menu's targeting rule (TREE-06): right-clicking a row acts on THAT row, so the row is made
		// current and the selection service is brought into agreement with it before the menu opens. The re-publish in
		// the already-current case is not redundant -- the service can lag the view (a no-reveal FormField write-back
		// tracks a form field while the tree highlight stays put), and the menu's commands read the SERVICE. Public
		// because the rule is testable while the menu itself is not: QMenu::exec blocks on a nested event loop no
		// offscreen test can drive.

		void select_context_row ( const QModelIndex& target );

		void expand_all   ();                     // TREE-05.
		void collapse_all ();

		void expand_current_subtree   ();         // TREE-05 -- the per-node pair, acting on the CURRENT row.
		void collapse_current_subtree ();

		// Does the tree's current row have children? The Expand / Collapse Subtree pair acts on that row, so their
		// enabled state has to ask the same question they will act on. The SELECTION is not that question: a no-reveal
		// selection deliberately leaves the current row where it is (EDITOR-04), so the two can legitimately differ.

		bool current_row_is_branch () const;

		//=============================================================================================================
		// QObject
		//=============================================================================================================

	protected:

		// Watches the tree for a KEYBOARD-originated context menu (the Menu key, Shift+F10). Qt hands a custom context
		// menu nothing but a position, and for the keyboard case that position comes from the input-method cursor rect
		// rather than from the current row -- so a menu built from it targets the first visible row, or nothing. The
		// reason is only available on the event itself, which is why this is a filter rather than a smarter handler.

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The two GESTURE channels, both distinct from selection -- which is a separate, already-live channel
		// (SelectionService) that these say nothing about.
		//
		// The distinction is the whole point. A selection change means "the highlight is now here", and it happens just
		// as much when the user holds Down as when they click; answering it by handing over the editing caret takes the
		// keyboard out of the tree mid-navigation. Only a gesture is a request to start editing, so only a gesture is
		// published as one.

		// A single click on a row. Whether it activates editing is the receiving view's "Edit on" setting (EDITOR-04,
		// SET-05 / SET-07). Not emitted for a click on the branch chevron -- that gesture is expansion, not selection.

		void node_clicked ( const JsonPointer& pointer );

		// The unconditional activation gesture: Enter, or a double-click.

		void node_activated ( const JsonPointer& pointer );

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_tree_selection_changed  ();
		void handle_service_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin );
		void handle_service_selection_cleared ();

		// The removal window (Q22's array side). Qt's selection model moves a dying current row to a sibling INSIDE
		// beginRemoveRows -- when the document has already mutated but the shadow still holds pre-removal positions.
		// An object member's pointer survives that window (the shadow stores its key), but an array element's token IS
		// its position, so a pointer published right there is stale by the size of the removed run. The pane therefore
		// asks the MODEL whether it is mid-removal (a signal-order flag of its own would be set too late: the
		// selection model connected first, at setModel time, so its fallback move runs before any slot of ours) and
		// defers the publication to rowsRemoved, when the shadow has renumbered and pointer_for_index answers in
		// post-removal coordinates.

		void handle_rows_removed ();

		void handle_model_reset          ();
		void handle_projection_capture   ();      // Save expansion + selection ahead of a rebuild.
		void handle_projection_restore   ();      // Put back whatever still resolves.

		void handle_clicked       ( const QModelIndex& index );   // Publishes the click gesture; changes nothing here.
		void handle_activated     ( const QModelIndex& index );   // Enter on a branch toggles it (NAV-02).
		void handle_context_menu  ( const QPoint& position );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// Is every branch above this row open -- i.e. is the row one the view actually draws? The no-reveal half of
		// EDITOR-04 turns on this question: a form-field write-back moves the highlight only to a row already on show.

		bool is_row_revealed ( const QModelIndex& index ) const;

		// Build and run the context menu for one row, anchored at one screen point (TREE-06). The two routes into it --
		// a right-click, and the Menu key / Shift+F10 -- differ only in which row and which point, so they share this.

		void show_context_menu ( const QModelIndex& target, const QPoint& globalPosition );

		// The keyboard route: the same menu, on the tree's current row, anchored beneath it.

		void show_context_menu_for_current_row ();

		void collect_expanded_pointers ( const QModelIndex& index, QList<JsonPointer>& outPointers ) const;

		// The one publication of the tree's current row to the service (flag-guarded against its own echo). Called by
		// handle_tree_selection_changed directly, or from handle_rows_removed when the selection changed inside a
		// removal window and the publication was held for the shadow to renumber.

		void publish_tree_selection ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//
		// The document and the icon library are consumed by the model, which is constructed from them and then owns the
		// relationship; the pane keeps no second reference to either.
		//=============================================================================================================

	private:

		SelectionService* selection;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		PaneHeader*    paneHeader = nullptr;
		QTreeView*     treeView   = nullptr;
		JsonTreeModel* treeModel  = nullptr;

		NodeContextActions contextActions;
		TreeViewCommands   viewCommands;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// Breaks the selection feedback loop: while the pane is applying a selection that came FROM the service, the
		// view's own selectionChanged must not write it straight back.

		bool applyingServiceSelection = false;

		// The other half of the same loop: while the pane is PUBLISHING the tree's selection to the service, the
		// service's synchronous re-emission must not be applied back. This is a MECHANISM guard, deliberately not an
		// origin test -- the origin is reveal intent and nothing else, and Phase 9's command layer legitimately writes
		// Tree-origin selections the pane must apply (a menu add selects and reveals the new node, NAV-03). Guarding
		// on origin == Tree instead was how the tree highlight stopped following command-driven selections, and the
		// context menu then acted on a stale row.

		bool publishingTreeSelection = false;

		// Whether a selectionChanged arrived inside the model's removal window (JsonTreeModel::removal_in_progress)
		// whose publication is being held for the shadow to renumber -- released by handle_rows_removed.

		bool publicationDeferredByRemoval = false;

		// Set between projection_about_to_rebuild and projection_rebuilt. It also suppresses the model-reset handler's
		// default "expand and select the root", which would otherwise fight the restore on a root subtree replacement --
		// that path emits BOTH modelReset and the projection pair.

		bool restoringProjection = false;

		QList<JsonPointer> capturedExpansion;
		JsonPointer        capturedSelection;
		bool               hadCapturedSelection = false;
	};
}

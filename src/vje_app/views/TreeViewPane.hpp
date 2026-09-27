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
//     - MULTIPLE selection and its one constraint (TREE-09). The view is ExtendedSelection, so Ctrl and Shift build a
//       set with no help from us; what this pane owns is the rule that the set lies within ONE PARENT, and what
//       happens when a gesture would take it outside one -- the whole gesture is refused, the previous selection is
//       put back, and the status bar says why. Reducing the set to the part that fits was rejected: a selection that
//       quietly does something narrower than it looked is one the user discovers only after acting on it.
//     - EDIT-10's drag-to-reorder, of which it owns only the translation: Qt runs the drag, JsonTreeModel says where a
//       drop may land, tree_drop_plan works out which children move, and this pane turns the drop into a request the
//       command layer answers with one undo step. It does not edit -- including the no-op drop, which is published
//       rather than swallowed so VAL-05 can report it.
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
#include "views/tree_drop_plan.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QColor>
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
	class TreeNodeDelegate;
	class StatusService;

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

		// The status service is nullable, which suppresses the status-bar half of TREE-09's refusal and nothing else --
		// the refusal itself still stands. That is JsonPathView's arrangement for QUERY-06, for the same reason: the
		// headless cases construct the pane without a window to post a message into.

		TreeViewPane
		(
			JsonDocument*     document,
			SelectionService* selection,
			IconLibrary*      icons,
			StatusService*    status = nullptr,
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

		// SET-14a: the unsaved-change dot's colour, as it shows in the theme in effect. Repaints the tree.

		void set_change_mark_colour ( const QColor& colour );

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

		// EDIT-10's drop, after Qt has decided WHERE it landed. Checks the same-parent rule a second time, builds the
		// reorder from the current selection, and publishes it as nodes_reorder_requested -- including the empty plan
		// of a drop that landed where the selection already was, which the command layer reports.
		//
		// Public for the reason select_context_row is: the rule is testable while the gesture is not, since QDrag::exec
		// blocks on a nested event loop no offscreen test can drive (the QMenu::exec problem again).

		void apply_drop ( const QModelIndex& dropParent, int dropRow );

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

		// EDIT-10. A drop that passed the same-parent rule, expressed as the reorder it asks for: the container whose
		// children move, the moves in the order they must be applied, and where the dragged run ends up so the command
		// layer can leave it selected.
		//
		// EMITTED EVEN WHEN moves IS EMPTY, and that is the point of emitting it at all rather than acting on it here.
		// An empty plan is a drop that landed where the selection already was -- the one reorder no-op the application
		// can reach -- and VAL-05 says a no-op is reported, which is the command layer's job and not a pane's.

		void nodes_reorder_requested
		(
			const JsonPointer&          parentPointer,
			const QList<TreeDropMove>&  moves,
			const QList<int>&           landingIndices
		);

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

		// NAV-03's post-delete landing, which became OURS when the view went ExtendedSelection (TREE-09). Under
		// SingleSelection, QAbstractItemView::rowsAboutToBeRemoved moved a dying current row to the FOLLOWING sibling
		// -- but that block is guarded on the mode, and without it QItemSelectionModel's own fallback takes the
		// PRECEDING row instead, which is the opposite of what NAV-03 states. Deleting a middle element silently
		// landed on the one above it. Stating the rule here rather than inheriting it also makes it testable, and
		// Phase 9's select_after_removal is only a correction on the menu route -- undo and redo of a removal ride
		// this publication with nothing behind them.

		void handle_rows_about_to_be_removed ( const QModelIndex& parent, int first, int last );

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

		// The one publication of the tree's selection to the service (flag-guarded against its own echo). Called by
		// handle_tree_selection_changed directly, or from handle_rows_removed when the selection changed inside a
		// removal window and the publication was held for the shadow to renumber.
		//
		// Publishes the SET and the PRIMARY together (TREE-04 / TREE-09). The primary is the tree's current row, which
		// is where the last gesture landed -- so the highlight, the editor pane and the Node path pane cannot disagree
		// about which node is the subject. The set is sorted into document order, which is well defined precisely
		// because the same-parent rule holds: every row in it is a sibling, so its row number is its position.

		void publish_tree_selection ();

		// The TREE-09 constraint and its refusal. Are the view's selected rows all children of one parent -- and, when
		// they are not, put back what the service still holds and say why.
		//
		// The restore reads the SERVICE rather than a remembered copy of the view's last good state, which is what
		// keeps this from needing bookkeeping of its own: the service IS the last accepted selection, since every
		// accepted one was published to it and a refused one never reaches it.

		bool selection_is_within_one_parent () const;
		void restore_selection_from_service ();
		void report_refused_extension       ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//
		// The document and the icon library are consumed by the model, which is constructed from them and then owns the
		// relationship; the pane keeps no second reference to either.
		//=============================================================================================================

	private:

		SelectionService* selection;
		StatusService*    status;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		PaneHeader*    paneHeader = nullptr;
		QTreeView*        treeView     = nullptr;
		JsonTreeModel*    treeModel    = nullptr;
		TreeNodeDelegate* nodeDelegate = nullptr;                // Owned by the view; paints TREE-10's dot.

		NodeContextActions contextActions;
		TreeViewCommands   viewCommands;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// Breaks the selection feedback loop: while the pane is applying a selection that came FROM the service, the
		// view's own selectionChanged must not write it straight back.
		//
		// TREE-09 gave it a second entry point rather than a second mechanism: rolling back a refused cross-parent
		// extension is also the pane writing the SERVICE's selection onto the view, so it is the same guard around the
		// same kind of write. What must never happen is a guard keyed on the origin of what came in (lesson D5).

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

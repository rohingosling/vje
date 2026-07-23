//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public slots:

		// Hand the keyboard to the tree (NAV-04). Focusing is not selecting: whatever row was current stays current.

		void take_focus ();

		void expand_all   ();                     // TREE-05.
		void collapse_all ();

		void expand_current_subtree   ();         // TREE-06 -- the context menu's per-node pair.
		void collapse_current_subtree ();

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

		void collect_expanded_pointers ( const QModelIndex& index, QList<JsonPointer>& outPointers ) const;

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

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// Breaks the selection feedback loop: while the pane is applying a selection that came FROM the service, the
		// view's own selectionChanged must not write it straight back.

		bool applyingServiceSelection = false;

		// Set between projection_about_to_rebuild and projection_rebuilt. It also suppresses the model-reset handler's
		// default "expand and select the root", which would otherwise fight the restore on a root subtree replacement --
		// that path emits BOTH modelReset and the projection pair.

		bool restoringProjection = false;

		QList<JsonPointer> capturedExpansion;
		JsonPointer        capturedSelection;
		bool               hadCapturedSelection = false;
	};
}

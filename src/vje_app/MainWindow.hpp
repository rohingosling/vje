//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   MainWindow -- the application shell. A QMainWindow hosting the menu
//   bar (File / Edit / Document / View / Help, section 2.3), the toolbar (section 2.4), the two-pane QSplitter
//   workspace (section 2.5), and the status bar (section 2.8). Commands are QActions shared between the menu and the
//   toolbar so enablement is one source of truth (disabled-not-hidden).
//
//   Scope as of Phase 7. The menu/toolbar STRUCTURE is complete and faithful to the spec; the wired commands are the
//   shell-level set (View > Theme via ThemeService, File > Exit, Help > About, command-line file open, FILE-01) plus
//   View > Expand All / Collapse All, which went live with the tree (TREE-05). Every other action is present, labelled,
//   and shortcut-bound but disabled -- its handler and centralized enablement arrive with its own phase (the full
//   command surface in Phase 9, the file lifecycle in Phase 10). Both cards are now live: the TreeViewPane on the left
//   and the EditorPane on the right, bridged by the SelectionService. Window/splitter geometry is persisted through the
//   SettingsStore (NFR-06).
//
//   All collaborators are injected by the composition root (main.cpp); MainWindow owns
//   none of them.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "services/ThemeService.hpp"
#include "views/NodeContextActions.hpp"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QSplitter;

namespace vje
{
	class JsonDocument;
	class UndoController;
	class SettingsStore;
	class SelectionService;
	class StatusService;
	class IconLibrary;
	class TreeViewPane;
	class EditorPane;
	class PaneCycler;

	//*****************************************************************************************************************
	// Class: MainWindow
	//*****************************************************************************************************************

	class MainWindow : public QMainWindow
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		MainWindow
		(
			JsonDocument*     document,
			UndoController*   undo,
			SettingsStore*    settings,
			ThemeService*     theme,
			SelectionService* selection,
			StatusService*    status,
			IconLibrary*      icons,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Load a JSON file into the document and reflect it in the title / status (FILE-01). Reports a load failure
		// with an error dialog (FILE-06) and returns false. The Open pipeline (dirty gate, off-thread I/O) is Phase 10;
		// this is the minimal command-line / reusable seam.

		bool open_document_from_path ( const QString& path );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void closeEvent ( QCloseEvent* event ) override;   // Persist geometry (the FILE-08 dirty gate lands in Phase 10).

		//=============================================================================================================
		// Construction Helpers
		//=============================================================================================================

	private:

		void create_actions    ();
		void create_menus      ();
		void create_toolbar    ();
		void create_workspace  ();
		void create_status_bar ();
		void wire_services     ();
		void restore_geometry  ();
		void persist_geometry  ();

		// Only the pane WIDTHS are user state; everything else QSplitter::saveState carries is a design constant.

		void restore_splitter_sizes ();

		// Assigns every action its icon from the IconLibrary. Re-invoked whenever the library re-tints, so the menu,
		// toolbar, and (from Phase 6) tree icons follow a theme switch.

		void apply_action_icons ();

		// The shared node command set, handed to every pane that offers the node context menu -- the tree (TREE-06) and
		// the Form View's key labels (EDITOR-02). Built here so there is exactly one mapping from command to action.

		NodeContextActions node_context_actions () const;

		//=============================================================================================================
		// Update Helpers
		//=============================================================================================================

	private:

		void update_title           ();
		void update_status_document ();
		void update_status_selection ();
		void update_tree_command_enablement ();
		void sync_theme_actions     ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		UndoController*   undo;
		SettingsStore*    settings;
		ThemeService*     theme;
		SelectionService* selection;
		StatusService*    status;
		IconLibrary*      icons;

		//=============================================================================================================
		// Data Members -- workspace and status widgets.
		//=============================================================================================================

	private:

		QSplitter*    workspaceSplitter = nullptr;
		TreeViewPane* treePane          = nullptr;   // The navigation tree (Phase 6).
		EditorPane*   editorPane        = nullptr;   // The tabbed editor views (Phase 7).
		PaneCycler*   paneCycler        = nullptr;   // Tab / Shift+Tab between the two (NAV-04).

		QLabel* statusDocumentLabel = nullptr;   // File name + modified indicator.
		QLabel* statusNodePathLabel = nullptr;   // Selection as a JSON Pointer.
		QLabel* statusNodeInfoLabel = nullptr;   // Selected node's type + child count.
		QLabel* statusCaretLabel    = nullptr;   // Ln/Col (Code View only; empty in Phase 5).

		//=============================================================================================================
		// Data Members -- command actions (shared by menu + toolbar; enablement is one source of truth).
		//=============================================================================================================

	private:

		// File.
		QAction* actionNew        = nullptr;
		QAction* actionOpen       = nullptr;
		QAction* actionClose      = nullptr;
		QAction* actionSave       = nullptr;
		QAction* actionSaveAs     = nullptr;
		QAction* actionPageSetup  = nullptr;
		QAction* actionPrint      = nullptr;
		QAction* actionSettings   = nullptr;
		QAction* actionExit       = nullptr;

		// Edit.
		QAction* actionFind  = nullptr;
		QAction* actionGoTo  = nullptr;
		QAction* actionUndo  = nullptr;
		QAction* actionRedo  = nullptr;
		QAction* actionCut   = nullptr;
		QAction* actionCopy  = nullptr;
		QAction* actionPaste = nullptr;

		// Document -- add commands.
		QAction* actionAddObject  = nullptr;
		QAction* actionAddArray   = nullptr;
		QAction* actionAddString  = nullptr;
		QAction* actionAddNumber  = nullptr;
		QAction* actionAddBoolean = nullptr;
		QAction* actionAddNull    = nullptr;

		// Document -- node operations and transforms.
		QAction* actionRenameKey        = nullptr;
		QAction* actionDuplicateNode    = nullptr;
		QAction* actionDeleteNode       = nullptr;
		QAction* actionMoveUp           = nullptr;
		QAction* actionMoveDown         = nullptr;
		QAction* actionNormalizeArray   = nullptr;
		QAction* actionArrayToObjects   = nullptr;
		QAction* actionObjectsToArray   = nullptr;

		// View.
		QAction* actionExpandAll   = nullptr;
		QAction* actionCollapseAll = nullptr;
		QAction* actionThemeLight  = nullptr;
		QAction* actionThemeDark   = nullptr;
		QAction* actionThemeSystem = nullptr;

		QActionGroup* themeActionGroup = nullptr;

		// Help.
		QAction* actionOnlineHelp     = nullptr;
		QAction* actionGettingStarted = nullptr;
		QAction* actionCheckUpdates   = nullptr;
		QAction* actionReleaseNotes   = nullptr;
		QAction* actionAbout          = nullptr;
	};
}

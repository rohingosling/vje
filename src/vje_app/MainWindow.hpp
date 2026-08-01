//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   MainWindow -- the application shell. A QMainWindow hosting the menu
//   bar (File / Edit / Document / View / Help, section 2.3), the toolbar (section 2.4), the two-pane QSplitter
//   workspace (section 2.5), and the status bar (section 2.8). Commands are QActions shared between the menu and the
//   toolbar so enablement is one source of truth (disabled-not-hidden).
//
//   Scope as of Phase 13. The menu/toolbar STRUCTURE is complete and faithful to the spec, and so is nearly all of its
//   behaviour: the shell-level set (View > Theme, Exit, About, Expand / Collapse All), the Edit and Document command
//   surface with its one focus-aware enablement pass (Phase 9), the File menu -- New / Open / Close / Save / Save As,
//   Recent Files, Import / Export, Settings, and now Page Setup and Print -- plus Find and Go To. Still
//   present-but-disabled: the Help web links and the update check (Phases 15/17).
//
//   The window DELEGATES rather than implements the file lifecycle: FileController owns the command sequences and their
//   two gates, and the window contributes the parts only it can -- the actions, the Recent Files menu, the drop target
//   (FILE-09), the EDITOR-09 view gate as a callback, and the enablement. Both workspace cards are live (TreeViewPane
//   and EditorPane, bridged by the SelectionService), and window/splitter geometry persists through the SettingsStore
//   (NFR-06).
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

#include "services/IDialogService.hpp"
#include "services/ThemeService.hpp"
#include "views/NodeContextActions.hpp"
#include "views/toolbar_plan.hpp"

#include <QList>
#include <QMainWindow>

#include <functional>
#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QSplitter;

namespace vje
{
	enum class JsonKind;

	class JsonDocument;
	class JsonNode;
	class JsonPointer;
	class UndoController;
	class SettingsStore;
	class SelectionService;
	class StatusService;
	class IconLibrary;
	class ClipboardService;
	class BackgroundIo;
	class DiagnosticLog;
	class FileController;
	class FindController;
	class PrintController;
	class Card;
	class FindBar;
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
			ClipboardService* clipboard,
			BackgroundIo*     io,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Open a JSON file through the full pipeline -- the EDITOR-09 view gate, the FILE-08 dirty gate, the off-thread
		// load, and FILE-06 error reporting (FileController::open_path). The command-line seam (FILE-01).

		bool open_document_from_path ( const QString& path );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void closeEvent ( QCloseEvent* event ) override;   // The EDITOR-09 and FILE-08 gates, then persist geometry.

		// FILE-09: a JSON file dropped onto the window opens through the same pipeline as File > Open.

		void dragEnterEvent ( QDragEnterEvent* event ) override;
		void dropEvent      ( QDropEvent* event ) override;

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

		void update_title            ();
		void update_status_document  ();
		void update_status_selection ();
		void sync_theme_actions      ();

		// The File > Recent Files submenu (FILE-05), rebuilt from the persisted list whenever it changes. An empty list
		// leaves a single disabled placeholder rather than an empty menu that opens onto nothing.

		void rebuild_recent_files_menu ();

		// SET-04: rebuild the toolbar to hold exactly what the user's layout asks for, in its order, with the separators
		// it places and the stray ones collapsed. Never hides the QAction (the menus share it) and never hides the
		// button (the layout would undo it) -- see views/toolbar_plan.hpp. Hides the BAR when the layout is empty.

		void apply_toolbar_layout ();

		// NAV-06: move the splitter by one keyboard step. Positive widens the tree pane, negative narrows it; where it
		// lands is nudged_splitter_sizes (views/WorkspaceSplitter.hpp), which is pure and tested without a window.

		void nudge_workspace_splitter ( int delta );

		// SET-03: carry "Rounded pane corners" to both cards. The window does this rather than the cards reading the
		// store themselves, because a Card is a general container widget and the question is asked on every repaint
		// (Card.hpp, NFR-03).

		void apply_pane_corner_setting ();

		// The Debug group's icon source. Pushes the stored choice into IconLibrary, which drops its cache and emits
		// icons_changed; every icon consumer already listens for that, so this one call re-icons the whole application
		// (config::icons::DEFAULT_ICON_SOURCE).

		void apply_icon_source_setting ();

		// File > Settings... (SET-01). Builds the schema (with the Toolbar group taken from this window's own command
		// catalogue) and runs the modal dialog; everything it changes reaches the rest of the application through the
		// store's change signal.

		void show_settings_dialog ();

		// Edit > Go To... (FIND-04). Runs the modal dialog pre-filled with the current selection's pointer; everything it
		// decides belongs to FindController, so the window contributes only the parent and the starting text.

		void show_go_to_dialog ();

		//=============================================================================================================
		// Command surface (Phase 9) -- the Edit and Document commands, node clipboard, and centralized enablement.
		//=============================================================================================================

	private:

		// The current selection resolved against the live document, or nullptr when there is none.

		JsonNode* selected_node () const;

		// Document > Add (EDIT-03 auto-placement); the six typed commands and the tree context menu's explicit
		// Add Child / Add Sibling (EDIT-04).

		void add_node_auto ( JsonKind kind );
		void add_child     ( JsonKind kind );
		void add_sibling   ( JsonKind kind );

		bool can_add_child   () const;
		bool can_add_sibling () const;

		// The §4 accelerators (Insert / Ctrl+Insert): pop up the six-type Add Child / Add Sibling chooser at the tree
		// selection, the keyboard counterpart of the tree context menu's submenus (EDIT-04).

		void popup_add_child_menu   ();
		void popup_add_sibling_menu ();

		// The shared chooser body: one menu populated from the ONE add-type table (NodeContextActions), anchored at
		// the tree's current row -- a keyboard gesture pops at the keyboard's subject, not at the mouse.

		void popup_add_type_chooser ( const std::function<void ( JsonKind )>& callback );

		// Prompt for an object member key. Returns a null QString when the user cancels; an empty (but non-null) string
		// is a legal key. Re-prompts on a duplicate so the flow is not lost.

		QString prompt_for_key ( const JsonNode& parentObject, const QString& title ) const;

		// Document node operations (EDIT-02, 05, 07, 08) and array transforms (EDIT-11..13).

		void rename_key        ();
		void duplicate_node    ();
		void delete_node       ();
		void move_node_up      ();
		void move_node_down    ();
		void normalize_array   ();
		void array_to_objects  ();
		void objects_to_array  ();

		// After a removal, move the selection to the following sibling, else the previous, else the parent (NAV-03).

		void select_after_removal ( const JsonPointer& parentPointer, int removedIndex );

		// Edit > Cut / Copy / Paste (EDIT-06), routed by focus: a text editor keeps its native clipboard, the array
		// table gets the cell clipboard while the editor pane holds the keyboard (EDITOR-11), and otherwise the tree
		// node clipboard acts on the selection.

		void handle_cut   ();
		void handle_copy  ();
		void handle_paste ();

		// Does the editor pane hold the keyboard? The gate on the cell-clipboard route: the array table keeps a
		// current cell while the tree has focus, so the route must ask about FOCUS, not about the cell (section 2.3).

		bool editor_pane_holds_focus () const;

		void copy_selected_node  ();
		void cut_selected_node   ();
		void paste_onto_selection ();

		// One place that sets the enabled state of every command Phase 9 owns, recomputed on any input that can change
		// it: selection, document, the undo stack, keyboard focus, and the clipboard (disabled-not-hidden).

		void update_command_enablement ();

		// The Cut / Copy / Paste routing slice of the enablement, callable alone: keyboard focus and the clipboard
		// change far more often than the document (every editor open / close, every copy anywhere in the OS), and
		// recomputing only this slice keeps those triggers away from the full document-shaped pass (NFR-03).
		// update_command_enablement() ends by calling it, so the full pass still covers everything.

		void update_clipboard_enablement ();

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
		ClipboardService* clipboard;
		BackgroundIo*     io;

		//=============================================================================================================
		// Data Members -- window-scoped collaborators (owned here).
		//
		// Unlike the injected services above, these two are scoped to THIS WINDOW: a modal dialog needs a parent window,
		// and the file commands are the window's own actions. Everything they depend on is still owned by the
		// composition root and passed in.
		//=============================================================================================================

	private:

		// Held as the INTERFACE, not as DialogService: what the window passes on to the controller is the seam, and the
		// concrete Qt implementation is chosen in one line of the constructor.

		std::unique_ptr<IDialogService> dialogs;
		FileController*                 fileController = nullptr;   // The file lifecycle; a child QObject of this window.
		FindController*                 findController = nullptr;   // Find and Go To (FIND-01..04); also a child QObject.
		PrintController*                printController = nullptr;  // Page Setup and Print (FILE-12); also a child QObject.
		DiagnosticLog*                  diagnostics    = nullptr;   // Opt-in logging (SET-09); also a child QObject.

		//=============================================================================================================
		// Data Members -- workspace and status widgets.
		//=============================================================================================================

	private:

		QToolBar*     mainToolBar       = nullptr;   // Kept for SET-04: its CONTENT is rebuilt from the user's layout.
		QSplitter*    workspaceSplitter = nullptr;
		TreeViewPane* treePane          = nullptr;   // The navigation tree (Phase 6).
		EditorPane*   editorPane        = nullptr;   // The tabbed editor views (Phase 7).
		PaneCycler*   paneCycler        = nullptr;   // Tab / Shift+Tab between the two (NAV-04).
		FindBar*      findBar           = nullptr;   // Docked above the editor pane, hidden until Ctrl+F (FIND-01).

		// The two card surfaces (STYLE-01/02). Held only so SET-03's "Rounded pane corners" can reach them when the
		// user changes it -- nothing else asks them anything.

		Card* treeCard   = nullptr;
		Card* editorCard = nullptr;

		QLabel* statusDocumentLabel = nullptr;   // File name + modified indicator.
		QLabel* statusNodePathLabel = nullptr;   // Selection as a JSON Pointer.
		QLabel* statusNodeInfoLabel = nullptr;   // Selected node's type + child count.
		QLabel* statusCaretLabel    = nullptr;   // Ln/Col (Code View only; empty in Phase 5).

		QMenu* recentFilesMenu = nullptr;        // File > Recent Files (FILE-05), rebuilt from the store.

		// File > Export's per-format items (FILE-11). Held because their enabled state is not one flag: each carries its
		// format id in QAction::data() and is asked of can_export() against the current selection.

		QList<QAction*> exportActions;

		// Every command that MAY sit on the toolbar, in menu order, each with its persistence name (SET-04). It is the
		// source of both the rendered bar and the Settings dialog's Toolbar group, so the eligible set is stated once --
		// here, by the window that owns the actions. What the bar actually carries is the user's LAYOUT over these
		// names, read from the store (views/toolbar_catalogue).

		std::vector<ToolbarCommand> toolBarCatalogue;

		// The separators currently on the bar. They are the ones this window created, kept so a rebuild can delete them.

		QList<QAction*> toolBarSeparators;

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

		// F3 / Shift+F3 (section 4). They carry no menu entry -- the Edit menu lists Find... and Go To... alone
		// (section 2.3) -- so like the EDIT-04 Insert accelerators they exist for their shortcuts.

		QAction* actionFindNext     = nullptr;
		QAction* actionFindPrevious = nullptr;

		// Copy JSON Pointer (FIND-05) -- Go To's inverse, and next to it in the Edit menu for that reason.

		QAction* actionCopyPointer = nullptr;

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
		// The Insert / Ctrl+Insert accelerators that pop up the Add Child / Add Sibling type chooser (EDIT-04, section 4).
		// Scoped to the tree, so they do not steal Insert from a text editor.

		QAction* actionAddChildAccelerator   = nullptr;
		QAction* actionAddSiblingAccelerator = nullptr;

		QAction* actionRenameKey        = nullptr;
		QAction* actionDuplicateNode    = nullptr;
		QAction* actionDeleteNode       = nullptr;
		QAction* actionMoveUp           = nullptr;
		QAction* actionMoveDown         = nullptr;
		QAction* actionNormalizeArray   = nullptr;
		QAction* actionArrayToObjects   = nullptr;
		QAction* actionObjectsToArray   = nullptr;

		// View. The whole-tree pair and the current-row pair, in that order in both surfaces that carry them: the View
		// menu and the tree's context menu (TREE-05 / TREE-06).

		QAction* actionExpandAll       = nullptr;
		QAction* actionCollapseAll     = nullptr;
		QAction* actionExpandSubtree   = nullptr;

		// NAV-06: pane width by keyboard, as two commands rather than a focus target (see create_actions).

		QAction* actionWidenTreePane   = nullptr;
		QAction* actionNarrowTreePane  = nullptr;
		QAction* actionCollapseSubtree = nullptr;
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

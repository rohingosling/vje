//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   MainWindow implementation -- builds the shell (menus, toolbar, workspace splitter, status bar), wires the commands,
//   and persists window/splitter geometry. See the header for the current scope: the File, Edit, Document, View, Find,
//   Go To and printing commands are live, and the Help links remain present-but-disabled placeholders owned by a later
//   phase.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "MainWindow.hpp"

#include "AppConfig.hpp"
#include "controllers/FileController.hpp"
#include "controllers/FindController.hpp"
#include "controllers/PrintController.hpp"
#include "controllers/converters.hpp"
#include "dialogs/GoToDialog.hpp"
#include "dialogs/SettingsDialog.hpp"
#include "dialogs/settings_schema.hpp"
#include "services/BackgroundIo.hpp"
#include "services/ClipboardService.hpp"
#include "services/DiagnosticLog.hpp"
#include "services/DialogService.hpp"
#include "services/IconLibrary.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"
#include "services/settings_profiles.hpp"
#include "views/Card.hpp"
#include "views/CodeView.hpp"
#include "views/EditorPane.hpp"
#include "views/FindBar.hpp"
#include "views/FormView.hpp"
#include "views/PaneCycler.hpp"
#include "views/TextView.hpp"
#include "views/toolbar_catalogue.hpp"
#include "views/toolbar_plan.hpp"
#include "views/TreeViewPane.hpp"
#include "views/WorkspaceSplitter.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/DocumentIo.hpp>
#include <vje_core/version.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QMimeData>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <utility>

namespace vje
{
	namespace
	{
		// Layout and limit constants live in AppConfig.hpp (config::window, config::workspace, config::limits).

		// A one-line description of a node for the status bar's node-info pane.

		QString describe_node ( const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::Object:
				{
					const int count = node.member_count ();

					return QObject::tr ( "object \xC2\xB7 %n member(s)", nullptr, count );
				}

				case JsonKind::Array:
				{
					const int count = node.array_size ();

					return QObject::tr ( "array \xC2\xB7 %n item(s)", nullptr, count );
				}

				case JsonKind::String:  return QObject::tr ( "string" );
				case JsonKind::Number:  return QObject::tr ( "number" );
				case JsonKind::Boolean: return QObject::tr ( "boolean" );
				case JsonKind::Null:    return QObject::tr ( "null" );
			}

			return QString ();
		}

		// The one local file a drop carries, or an empty string when it carries none (FILE-09). Only the FIRST is taken:
		// this is a single-document window, so opening several would mean discarding all but one of them anyway.

		QString first_dropped_local_file ( const QMimeData* mimeData )
		{
			if ( ( mimeData == nullptr ) || !mimeData->hasUrls () )
			{
				return QString ();
			}

			for ( const QUrl& url : mimeData->urls () )
			{
				if ( url.isLocalFile () )
				{
					return url.toLocalFile ();
				}
			}

			return QString ();
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	MainWindow::MainWindow
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
		QWidget*          parent
	)
		: QMainWindow ( parent )
		, document    ( document )
		, undo        ( undo )
		, settings    ( settings )
		, theme       ( theme )
		, selection   ( selection )
		, status      ( status )
		, icons       ( icons )
		, clipboard   ( clipboard )
		, io          ( io )
	{
		// The window-scoped pair, built before the actions that trigger them. The file controller is a child QObject, so
		// it dies with the window; the dialog service is parented on the window in the widget sense (its modals are).

		dialogs        = std::make_unique<DialogService> ( this );
		fileController = new FileController ( document, undo, settings, selection, status, dialogs.get (), io, this );
		findController = new FindController ( document, selection, status, clipboard, this );
		printController = new PrintController ( document, settings, dialogs.get (), status, this );
		diagnostics    = new DiagnosticLog ( settings, QString (), this );

		create_actions ();
		create_menus ();
		create_toolbar ();
		create_workspace ();
		create_status_bar ();
		wire_services ();

		apply_action_icons ();

		// FILE-09: the window itself is the drop target, so a file may be dropped anywhere on it rather than only on the
		// tree.

		setAcceptDrops ( true );

		// Reflect the (initially empty) document and theme state before restoring the persisted geometry.

		rebuild_recent_files_menu ();

		update_title ();
		update_status_document ();
		update_status_selection ();
		update_command_enablement ();
		sync_theme_actions ();

		restore_geometry ();

		// The log's first line, so a session's entries can be told apart and matched to a build (SET-09). A no-op unless
		// the user has turned logging on.

		diagnostics->write ( tr ( "Session started -- %1" ).arg ( version_line () ) );
	}

	//=================================================================================================================
	// Construction Helpers
	//=================================================================================================================

	void MainWindow::create_actions ()
	{
		// Every command exists as a QAction from the start so the menu and toolbar share one enablement source. Only
		// the shell-level commands are connected in Phase 5; the rest are disabled until their owning phase wires a
		// handler and its enablement (see the header scope note).

		// -- File ---------------------------------------------------------------------------------------------------

		actionNew       = new QAction ( tr ( "&New" ),        this );
		actionOpen      = new QAction ( tr ( "&Open..." ),    this );
		actionClose     = new QAction ( tr ( "&Close" ),      this );
		actionSave      = new QAction ( tr ( "&Save" ),       this );
		actionSaveAs    = new QAction ( tr ( "Save &As..." ), this );
		actionPageSetup = new QAction ( tr ( "Page Set&up..." ), this );
		actionPrint     = new QAction ( tr ( "&Print..." ),   this );
		actionSettings  = new QAction ( tr ( "&Settings..." ), this );
		actionExit      = new QAction ( tr ( "E&xit" ),       this );

		actionNew    ->setShortcut ( QKeySequence::New );
		actionOpen   ->setShortcut ( QKeySequence::Open );
		actionClose  ->setShortcut ( QKeySequence::Close );
		actionSave   ->setShortcut ( QKeySequence::Save );
		actionSaveAs ->setShortcut ( QKeySequence::SaveAs );
		actionPrint  ->setShortcut ( QKeySequence::Print );
		actionExit   ->setShortcut ( QKeySequence ( QStringLiteral ( "Ctrl+Q" ) ) );

		// -- Edit ---------------------------------------------------------------------------------------------------

		actionFind  = new QAction ( tr ( "&Find..." ),  this );
		actionGoTo  = new QAction ( tr ( "&Go To..." ), this );
		actionUndo  = new QAction ( tr ( "&Undo" ),     this );
		actionRedo  = new QAction ( tr ( "&Redo" ),     this );
		actionCut   = new QAction ( tr ( "Cu&t" ),      this );
		actionCopy  = new QAction ( tr ( "&Copy" ),     this );
		actionPaste = new QAction ( tr ( "&Paste" ),    this );

		actionFind  ->setShortcut ( QKeySequence::Find );
		actionGoTo  ->setShortcut ( QKeySequence ( QStringLiteral ( "Ctrl+G" ) ) );

		// FIND-02's F3 / Shift+F3. They carry no menu entry (section 2.3's Edit menu is Find... and Go To... alone), so
		// like the EDIT-04 Insert accelerators they are actions purely for their shortcuts -- and window-scoped
		// deliberately, since repeating a search is meant to work from wherever the user is, including the find field
		// itself and an open Code View.

		actionFindNext     = new QAction ( tr ( "Find Next" ),     this );
		actionFindPrevious = new QAction ( tr ( "Find Previous" ), this );

		actionFindNext    ->setShortcut ( QKeySequence ( Qt::Key_F3 ) );
		actionFindPrevious->setShortcut ( QKeySequence ( Qt::SHIFT | Qt::Key_F3 ) );

		addAction ( actionFindNext );
		addAction ( actionFindPrevious );

		// FIND-05, beside Go To because it is Go To's inverse: what it copies is precisely the text that dialog accepts.
		// Ctrl+Shift+C is free here and is the platform convention for "copy as path".

		actionCopyPointer = new QAction ( tr ( "Copy JSON &Pointer" ), this );

		actionCopyPointer->setShortcut ( QKeySequence ( QStringLiteral ( "Ctrl+Shift+C" ) ) );

		actionUndo  ->setShortcut ( QKeySequence::Undo );
		actionRedo  ->setShortcut ( QKeySequence::Redo );
		actionCut   ->setShortcut ( QKeySequence::Cut );
		actionCopy  ->setShortcut ( QKeySequence::Copy );
		actionPaste ->setShortcut ( QKeySequence::Paste );

		// -- Document (add commands) --------------------------------------------------------------------------------

		actionAddObject  = new QAction ( tr ( "Add &Object" ),  this );
		actionAddArray   = new QAction ( tr ( "Add &Array" ),   this );
		actionAddString  = new QAction ( tr ( "Add &String" ),  this );
		actionAddNumber  = new QAction ( tr ( "Add &Number" ),  this );
		actionAddBoolean = new QAction ( tr ( "Add &Boolean" ), this );
		actionAddNull    = new QAction ( tr ( "Add N&ull" ),    this );

		// -- Document (node operations and transforms) --------------------------------------------------------------

		actionRenameKey      = new QAction ( tr ( "&Rename Key..." ),         this );
		actionDuplicateNode  = new QAction ( tr ( "&Duplicate Node" ),        this );
		actionDeleteNode     = new QAction ( tr ( "De&lete Node" ),           this );
		actionMoveUp         = new QAction ( tr ( "Move &Up" ),               this );
		actionMoveDown       = new QAction ( tr ( "Move &Down" ),             this );
		actionNormalizeArray = new QAction ( tr ( "&Normalize Array Elements" ), this );
		actionArrayToObjects = new QAction ( tr ( "Convert Array to &Objects" ), this );
		actionObjectsToArray = new QAction ( tr ( "Convert Objects to A&rray" ), this );

		actionRenameKey     ->setShortcut ( QKeySequence ( Qt::Key_F2 ) );
		actionDuplicateNode ->setShortcut ( QKeySequence ( QStringLiteral ( "Ctrl+D" ) ) );
		actionDeleteNode    ->setShortcut ( QKeySequence::Delete );

		// EDIT-04 accelerators. They carry no menu of their own -- they pop up the type chooser -- so they exist only for
		// their shortcuts, scoped to the tree in wire_services().

		actionAddChildAccelerator   = new QAction ( tr ( "Add Child" ),   this );
		actionAddSiblingAccelerator = new QAction ( tr ( "Add Sibling" ), this );

		actionAddChildAccelerator  ->setShortcut ( QKeySequence ( Qt::Key_Insert ) );
		actionAddSiblingAccelerator->setShortcut ( QKeySequence ( QStringLiteral ( "Ctrl+Ins" ) ) );

		// -- View ---------------------------------------------------------------------------------------------------

		actionExpandAll       = new QAction ( tr ( "&Expand All" ),       this );
		actionCollapseAll     = new QAction ( tr ( "&Collapse All" ),     this );
		actionExpandSubtree   = new QAction ( tr ( "E&xpand Subtree" ),   this );
		actionCollapseSubtree = new QAction ( tr ( "Co&llapse Subtree" ), this );

		// NAV-06. The splitter is a mouse affordance and nothing else, so pane width was the one part of the workspace
		// a keyboard could not reach (NFR-05). Two COMMANDS rather than a focus target: the splitter never takes focus,
		// so NAV-04's ring stays the two panes and Tab keeps costing one press to cross the workspace.
		//
		// Alt+Shift+arrow, and the pair was chosen by elimination rather than taste. Plain Alt+arrow is the Settings
		// dialog's transfer list (SET-04); Ctrl+Shift+arrow selects by word in every text field and the Code View;
		// Ctrl+Alt+arrow is the screen-rotation hotkey some Windows graphics drivers install. That a stock
		// QPlainTextEdit does nothing with Alt+Shift+arrow is asserted in tst_code_view rather than assumed (D13).

		actionWidenTreePane  = new QAction ( tr ( "&Widen Tree Pane" ),  this );
		actionNarrowTreePane = new QAction ( tr ( "&Narrow Tree Pane" ), this );

		actionWidenTreePane ->setShortcut ( QKeySequence ( Qt::ALT | Qt::SHIFT | Qt::Key_Right ) );
		actionNarrowTreePane->setShortcut ( QKeySequence ( Qt::ALT | Qt::SHIFT | Qt::Key_Left ) );

		actionThemeLight  = new QAction ( tr ( "&Light" ),  this );
		actionThemeDark   = new QAction ( tr ( "&Dark" ),   this );
		actionThemeSystem = new QAction ( tr ( "&System" ), this );

		themeActionGroup = new QActionGroup ( this );

		for ( QAction* themeAction : { actionThemeLight, actionThemeDark, actionThemeSystem } )
		{
			themeAction->setCheckable ( true );
			themeActionGroup->addAction ( themeAction );
		}

		// -- Help ---------------------------------------------------------------------------------------------------

		actionOnlineHelp     = new QAction ( tr ( "Online &Help" ),       this );
		actionGettingStarted = new QAction ( tr ( "&Getting Started" ),   this );
		actionCheckUpdates   = new QAction ( tr ( "Check for &Updates..." ), this );
		actionReleaseNotes   = new QAction ( tr ( "&Release Notes" ),     this );
		actionAbout          = new QAction ( tr ( "&About VJE" ),         this );

		actionOnlineHelp->setShortcut ( QKeySequence::HelpContents );

		// -- Live shell-level wiring (Phase 5) ----------------------------------------------------------------------

		connect ( actionExit,  &QAction::triggered, this, &MainWindow::close );
		connect ( actionAbout, &QAction::triggered, this,
			[ this ] ()
			{
				QMessageBox::about
				(
					this,
					tr ( "About VJE" ),
					tr
					(
						"<b>VJE</b> \xE2\x80\x94 <b>V</b>ersatile <b>J</b>SON <b>E</b>ditor<br>"
						"Version %1<br>\xC2\xA9 Rohin Gosling.<br>"
					).arg ( version_string () )
				);
			} );

		connect ( actionThemeLight,  &QAction::triggered, this, [ this ] () { theme->set_theme ( Theme::Light );  } );
		connect ( actionThemeDark,   &QAction::triggered, this, [ this ] () { theme->set_theme ( Theme::Dark );   } );
		connect ( actionThemeSystem, &QAction::triggered, this, [ this ] () { theme->set_theme ( Theme::System ); } );

		// -- Phase 9: the Edit and Document command surface (EDIT-01..13, EDIT-06, EDITOR-11 routing) ----------------

		connect ( actionUndo, &QAction::triggered, this, [ this ] () { undo->undo (); } );
		connect ( actionRedo, &QAction::triggered, this, [ this ] () { undo->redo (); } );

		connect ( actionCut,   &QAction::triggered, this, &MainWindow::handle_cut );
		connect ( actionCopy,  &QAction::triggered, this, &MainWindow::handle_copy );
		connect ( actionPaste, &QAction::triggered, this, &MainWindow::handle_paste );

		connect ( actionAddObject,  &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::Object );  } );
		connect ( actionAddArray,   &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::Array );   } );
		connect ( actionAddString,  &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::String );  } );
		connect ( actionAddNumber,  &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::Number );  } );
		connect ( actionAddBoolean, &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::Boolean ); } );
		connect ( actionAddNull,    &QAction::triggered, this, [ this ] () { add_node_auto ( JsonKind::Null );    } );

		connect ( actionRenameKey,      &QAction::triggered, this, &MainWindow::rename_key );
		connect ( actionDuplicateNode,  &QAction::triggered, this, &MainWindow::duplicate_node );
		connect ( actionDeleteNode,     &QAction::triggered, this, &MainWindow::delete_node );
		connect ( actionMoveUp,         &QAction::triggered, this, &MainWindow::move_node_up );
		connect ( actionMoveDown,       &QAction::triggered, this, &MainWindow::move_node_down );
		connect ( actionNormalizeArray, &QAction::triggered, this, &MainWindow::normalize_array );
		connect ( actionArrayToObjects, &QAction::triggered, this, &MainWindow::array_to_objects );
		connect ( actionObjectsToArray, &QAction::triggered, this, &MainWindow::objects_to_array );

		connect ( actionAddChildAccelerator,   &QAction::triggered, this, &MainWindow::popup_add_child_menu );
		connect ( actionAddSiblingAccelerator, &QAction::triggered, this, &MainWindow::popup_add_sibling_menu );

		// -- Phase 10: the file lifecycle (FILE-01..09). Every one of these runs the EDITOR-09 view gate and the FILE-08
		//    dirty gate inside the controller, so the window does nothing but forward the command.

		connect ( actionNew,    &QAction::triggered, this, [ this ] () { fileController->new_document (); } );
		connect ( actionOpen,   &QAction::triggered, this, [ this ] () { fileController->open (); } );
		connect ( actionClose,  &QAction::triggered, this, [ this ] () { fileController->close_document (); } );
		connect ( actionSave,   &QAction::triggered, this, [ this ] () { fileController->save (); } );
		connect ( actionSaveAs, &QAction::triggered, this, [ this ] () { fileController->save_as (); } );

		connect ( actionSettings, &QAction::triggered, this, &MainWindow::show_settings_dialog );

		// -- Phase 13: printing (FILE-12). Both commands are the controller's whole; the window forwards and nothing
		//    more. Neither runs the EDITOR-09 view gate, deliberately -- see PrintController's header.

		connect ( actionPageSetup, &QAction::triggered, this, [ this ] () { printController->page_setup (); } );
		connect ( actionPrint,     &QAction::triggered, this, [ this ] () { printController->print (); } );

		// Everything still below the wired level is disabled until its owning phase: the Help web links and updates
		// (Phases 15/17). The Phase 9 to 13 commands get their enabled state from update_command_enablement(), not from
		// this list.

		const QList<QAction*> deferredActions =
		{
			actionOnlineHelp, actionGettingStarted, actionCheckUpdates, actionReleaseNotes
		};

		for ( QAction* deferred : deferredActions )
		{
			deferred->setEnabled ( false );
		}
	}

	void MainWindow::create_menus ()
	{
		// File.

		QMenu* fileMenu = menuBar ()->addMenu ( tr ( "&File" ) );
		fileMenu->addAction ( actionNew );
		fileMenu->addAction ( actionOpen );
		fileMenu->addAction ( actionClose );
		fileMenu->addSeparator ();
		fileMenu->addAction ( actionSave );
		fileMenu->addAction ( actionSaveAs );
		fileMenu->addSeparator ();

		// FILE-11: both submenus are built from the ONE registered converter table, so a fourth format is one entry in it
		// rather than two menu edits. The export items are kept in a list because each carries a per-selection TOOLTIP
		// naming what would block it -- refreshed by update_command_enablement, which is also where the wording lives.

		QMenu* const importMenu = fileMenu->addMenu ( tr ( "&Import" ) );
		QMenu* const exportMenu = fileMenu->addMenu ( tr ( "&Export" ) );

		// Qt suppresses action tooltips inside a menu unless asked; without this the blocker would be written and never
		// read. It applies to the enabled items too, where the tooltip is empty and so shows nothing.

		exportMenu->setToolTipsVisible ( true );

		for ( const ConverterFormat& format : registered_converters () )
		{
			const QString formatId = format.id;

			if ( format.supportsImport )
			{
				QAction* const importAction = importMenu->addAction ( format.displayName );

				connect ( importAction, &QAction::triggered, this, [ this, formatId ] ()
				{
					const ConverterFormat* const chosen = find_converter ( formatId );

					if ( chosen != nullptr )
					{
						fileController->import_document ( *chosen );
					}
				} );
			}

			if ( format.supportsExport )
			{
				QAction* const exportAction = exportMenu->addAction ( format.displayName );

				exportAction->setData ( formatId );

				connect ( exportAction, &QAction::triggered, this, [ this, formatId ] ()
				{
					const ConverterFormat* const chosen = find_converter ( formatId );

					if ( chosen != nullptr )
					{
						fileController->export_document ( *chosen );
					}
				} );

				exportActions.append ( exportAction );
			}
		}

		fileMenu->addSeparator ();
		fileMenu->addAction ( actionPageSetup );
		fileMenu->addAction ( actionPrint );
		fileMenu->addSeparator ();

		// FILE-05. Kept as a member because its contents are data, not structure: it is rebuilt from the persisted list
		// every time that list changes.

		recentFilesMenu = fileMenu->addMenu ( tr ( "&Recent Files" ) );

		fileMenu->addSeparator ();
		fileMenu->addAction ( actionSettings );
		fileMenu->addSeparator ();
		fileMenu->addAction ( actionExit );

		// Edit.

		QMenu* editMenu = menuBar ()->addMenu ( tr ( "&Edit" ) );
		editMenu->addAction ( actionFind );
		editMenu->addAction ( actionGoTo );

		// FIND-05 sits with the navigation pair rather than down in the clipboard group: what it produces is Go To's
		// input, and the whole value of the command is that it feeds the item directly above it.

		editMenu->addAction ( actionCopyPointer );
		editMenu->addSeparator ();
		editMenu->addAction ( actionUndo );
		editMenu->addAction ( actionRedo );
		editMenu->addSeparator ();
		editMenu->addAction ( actionCut );
		editMenu->addAction ( actionCopy );
		editMenu->addAction ( actionPaste );

		// Document.

		QMenu* documentMenu = menuBar ()->addMenu ( tr ( "&Document" ) );
		documentMenu->addAction ( actionAddObject );
		documentMenu->addAction ( actionAddArray );
		documentMenu->addAction ( actionAddString );
		documentMenu->addAction ( actionAddNumber );
		documentMenu->addAction ( actionAddBoolean );
		documentMenu->addAction ( actionAddNull );
		documentMenu->addSeparator ();
		documentMenu->addAction ( actionRenameKey );
		documentMenu->addAction ( actionDuplicateNode );
		documentMenu->addAction ( actionDeleteNode );
		documentMenu->addSeparator ();
		documentMenu->addAction ( actionMoveUp );
		documentMenu->addAction ( actionMoveDown );
		documentMenu->addSeparator ();
		documentMenu->addAction ( actionNormalizeArray );
		documentMenu->addAction ( actionArrayToObjects );
		documentMenu->addAction ( actionObjectsToArray );

		// View.

		QMenu* viewMenu = menuBar ()->addMenu ( tr ( "&View" ) );
		viewMenu->addAction ( actionExpandAll );
		viewMenu->addAction ( actionCollapseAll );
		viewMenu->addAction ( actionExpandSubtree );
		viewMenu->addAction ( actionCollapseSubtree );
		viewMenu->addSeparator ();

		// In the menu because a shortcut nobody can discover is not a command anyone has (NAV-06).

		viewMenu->addAction ( actionWidenTreePane );
		viewMenu->addAction ( actionNarrowTreePane );
		viewMenu->addSeparator ();

		QMenu* themeMenu = viewMenu->addMenu ( tr ( "&Theme" ) );
		themeMenu->addAction ( actionThemeLight );
		themeMenu->addAction ( actionThemeDark );
		themeMenu->addAction ( actionThemeSystem );

		// Help.

		QMenu* helpMenu = menuBar ()->addMenu ( tr ( "&Help" ) );
		helpMenu->addAction ( actionOnlineHelp );
		helpMenu->addAction ( actionGettingStarted );
		helpMenu->addSeparator ();
		helpMenu->addAction ( actionCheckUpdates );
		helpMenu->addAction ( actionReleaseNotes );
		helpMenu->addSeparator ();
		helpMenu->addAction ( actionAbout );
	}

	void MainWindow::create_toolbar ()
	{
		// The toolbar mirrors the most frequent commands, sharing the QActions with the menu so enablement stays in
		// one place: New / Open / Close / Save / Save As | Undo / Redo | the six Add commands.

		QToolBar* const toolBar = addToolBar ( tr ( "Main Toolbar" ) );
		toolBar->setObjectName ( QStringLiteral ( "mainToolBar" ) );   // Stable id for saveState/restoreState.
		toolBar->setMovable ( false );

		mainToolBar = toolBar;

		// Compact icon squares. With the label off screen every button has to carry its own
		// tooltip, so one is built below from the action's text plus its shortcut -- the toolbar teaches the keyboard.

		toolBar->setToolButtonStyle ( Qt::ToolButtonIconOnly );

		// The glyph size is a declared dial rather than the style's default (which happened to be the same 16): the
		// button square follows the icon, so config::toolbar::ICON_SIZE is the one number that resizes the toolbar.

		toolBar->setIconSize ( QSize ( config::toolbar::ICON_SIZE, config::toolbar::ICON_SIZE ) );

		// The CATALOGUE: every command that may appear on the toolbar, in menu order -- which is the order the Settings
		// dialog's Available list shows (SET-04). It is deliberately WIDER than the default layout; the commands not in
		// section 2.4's four groups are exactly what that list offers on a first run.
		//
		// Its bound is the icon set. The buttons are icon-only, so a command with no glyph could only ever render as a
		// blank square -- this list therefore mirrors apply_action_icons()'s table and nothing else may join it without
		// a glyph joining first (architecture.md section 9.2).
		//
		// The second column is the entry's persistence NAME: untranslated, independent of the label, and stable across
		// releases, because it is what a user's stored layout is written in. The fifteen that predate 2026-07-27 are
		// spelled exactly as the superseded toolbar.visible.* keys spelled them, which is what lets the migration in
		// stored_toolbar_layout() be a rename-free lookup.

		namespace names = toolbar_names;

		toolBarCatalogue =
		{
			{ actionNew,            names::NEW },
			{ actionOpen,           names::OPEN },
			{ actionClose,          names::CLOSE },
			{ actionSave,           names::SAVE },
			{ actionSaveAs,         names::SAVE_AS },
			{ actionPageSetup,      names::PAGE_SETUP },
			{ actionPrint,          names::PRINT },
			{ actionSettings,       names::SETTINGS },

			{ actionFind,           names::FIND },
			{ actionGoTo,           names::GO_TO },
			{ actionCopyPointer,    names::COPY_POINTER },
			{ actionUndo,           names::UNDO },
			{ actionRedo,           names::REDO },
			{ actionCut,            names::CUT },
			{ actionCopy,           names::COPY },
			{ actionPaste,          names::PASTE },

			{ actionAddObject,      names::ADD_OBJECT },
			{ actionAddArray,       names::ADD_ARRAY },
			{ actionAddString,      names::ADD_STRING },
			{ actionAddNumber,      names::ADD_NUMBER },
			{ actionAddBoolean,     names::ADD_BOOLEAN },
			{ actionAddNull,        names::ADD_NULL },

			{ actionRenameKey,      names::RENAME_KEY },
			{ actionDuplicateNode,  names::DUPLICATE_NODE },
			{ actionDeleteNode,     names::DELETE_NODE },
			{ actionMoveUp,         names::MOVE_UP },
			{ actionMoveDown,       names::MOVE_DOWN },

			{ actionNormalizeArray, names::NORMALIZE_ARRAY },
			{ actionArrayToObjects, names::ARRAY_TO_OBJECTS },
			{ actionObjectsToArray, names::OBJECTS_TO_ARRAY },

			{ actionExpandAll,      names::EXPAND_ALL },
			{ actionCollapseAll,    names::COLLAPSE_ALL },

			{ actionAbout,          names::ABOUT }
		};

		for ( const ToolbarCommand& command : toolBarCatalogue )
		{
			command.action->setObjectName ( command.name );

			// Every catalogue command needs a tooltip, not only the ones on the bar today -- any of them can be put
			// there, and with the label off screen the tooltip is the only thing naming the button.

			const QString label    = toolbar_command_label ( command.action );
			const QString shortcut = command.action->shortcut ().toString ( QKeySequence::NativeText );

			command.action->setToolTip ( shortcut.isEmpty () ? label : tr ( "%1 (%2)" ).arg ( label, shortcut ) );
		}

		// The toolbar's CONTENTS come from the plan, here and on every later layout change.

		apply_toolbar_layout ();
	}

	void MainWindow::nudge_workspace_splitter ( int delta )
	{
		// NAV-06. The decision -- how far, and what happens at each end -- is nudged_splitter_sizes, which is pure and
		// pinned headlessly; this is the whole of the window's share of it.

		workspaceSplitter->setSizes
		(
			nudged_splitter_sizes
			(
				workspaceSplitter->sizes (),
				delta,
				config::workspace::MINIMUM_TREE_PANE_WIDTH,
				config::workspace::MINIMUM_TREE_PANE_WIDTH
			)
		);
	}

	void MainWindow::apply_pane_corner_setting ()
	{
		// One reader (services/settings_profiles), two consumers -- the same shape SET-05a's key editing has, and for
		// the same reason: two surfaces that must never disagree about one setting.

		const bool rounded = rounded_pane_corners ( settings );

		if ( treeCard != nullptr )
		{
			treeCard->set_top_corners_rounded ( rounded );
		}

		if ( editorCard != nullptr )
		{
			editorCard->set_top_corners_rounded ( rounded );
		}
	}

	void MainWindow::apply_toolbar_layout ()
	{
		// SET-04, and it has to be CONTENT rather than a hidden widget. The QAction is shared with the menu bar and the
		// context menus, so hiding the action would remove the command from the whole application; and hiding the tool
		// BUTTON does not hold, because QToolBar's layout re-derives each button's visibility from its action's on every
		// relayout -- a directly hidden button comes back on its own (lessons-learned.md Q23). So the toolbar is rebuilt
		// to contain exactly what the user's layout asks for (views/toolbar_plan.hpp).

		for ( QAction* const existing : mainToolBar->actions () )
		{
			mainToolBar->removeAction ( existing );
		}

		// The separators are ours -- created by addSeparator, and no longer referenced by anything once removed.

		qDeleteAll ( toolBarSeparators );

		toolBarSeparators.clear ();

		// Reading the layout is not a plain lookup: an absent key means the default, a stored empty list means a
		// deliberately empty toolbar, and a settings file predating 2026-07-27 is migrated here on its first read.

		const std::vector<ToolbarItem> plan = toolbar_plan ( stored_toolbar_layout ( settings, toolBarCatalogue ), toolBarCatalogue );

		for ( const ToolbarItem& item : plan )
		{
			if ( item.separator )
			{
				toolBarSeparators.append ( mainToolBar->addSeparator () );
			}
			else
			{
				mainToolBar->addAction ( item.action );
			}
		}

		// An empty layout is legal (SET-04), and a bar with nothing in it reads as a broken strip rather than as a
		// choice -- so it is hidden outright. File > Settings brings it back.

		mainToolBar->setVisible ( !plan.empty () );
	}

	void MainWindow::apply_action_icons ()
	{
		if ( icons == nullptr )
		{
			return;
		}

		// One table so the command -> glyph mapping is reviewable in a single place, and it is also the ELIGIBILITY list
		// behind SET-04: the toolbar is icon-only, so a command with no glyph could only ever render as a blank square
		// and toolbar_catalogue offers exactly what is assigned here.
		//
		// Deliberately unadorned, and therefore deliberately not offerable on the toolbar: Exit, the theme radio items,
		// the Help entries, and the two subtree expand/collapse commands -- menu conventions leave those bare, an icon on
		// a checkable radio item fights its check mark, and the subtree pair reads as the whole-tree pair at icon size.
		// The three array transforms LEFT that set on 2026-07-27 and gained glyphs, precisely so they could be added to
		// the bar. The six type glyphs the library also carries are the tree's (TREE-03), consumed in Phase 6.

		const QList<QPair<QAction*, QString>> iconAssignments =
		{
			{ actionNew,           icon_names::DOCUMENT_NEW        },
			{ actionOpen,          icon_names::DOCUMENT_OPEN       },
			{ actionClose,         icon_names::DOCUMENT_CLOSE      },
			{ actionSave,          icon_names::DOCUMENT_SAVE       },
			{ actionSaveAs,        icon_names::DOCUMENT_SAVE_AS    },
			{ actionPageSetup,     icon_names::DOCUMENT_PAGE_SETUP },
			{ actionPrint,         icon_names::DOCUMENT_PRINT      },
			{ actionSettings,      icon_names::SETTINGS            },

			{ actionFind,          icon_names::EDIT_FIND           },
			{ actionGoTo,          icon_names::GO_TO               },
			{ actionCopyPointer,   icon_names::COPY_POINTER        },
			{ actionUndo,          icon_names::EDIT_UNDO           },
			{ actionRedo,          icon_names::EDIT_REDO           },
			{ actionCut,           icon_names::EDIT_CUT            },
			{ actionCopy,          icon_names::EDIT_COPY           },
			{ actionPaste,         icon_names::EDIT_PASTE          },

			{ actionAddObject,     icon_names::ADD_OBJECT          },
			{ actionAddArray,      icon_names::ADD_ARRAY           },
			{ actionAddString,     icon_names::ADD_STRING          },
			{ actionAddNumber,     icon_names::ADD_NUMBER          },
			{ actionAddBoolean,    icon_names::ADD_BOOLEAN         },
			{ actionAddNull,       icon_names::ADD_NULL            },

			{ actionRenameKey,     icon_names::NODE_RENAME         },
			{ actionDuplicateNode, icon_names::NODE_DUPLICATE      },
			{ actionDeleteNode,    icon_names::NODE_DELETE         },
			{ actionMoveUp,        icon_names::NODE_MOVE_UP        },
			{ actionMoveDown,      icon_names::NODE_MOVE_DOWN      },

			{ actionNormalizeArray, icon_names::NORMALIZE_ARRAY    },
			{ actionArrayToObjects, icon_names::ARRAY_TO_OBJECTS   },
			{ actionObjectsToArray, icon_names::OBJECTS_TO_ARRAY   },

			{ actionExpandAll,     icon_names::EXPAND_ALL          },
			{ actionCollapseAll,   icon_names::COLLAPSE_ALL        },

			{ actionAbout,         icon_names::ABOUT               }
		};

		for ( const auto& [ action, iconName ] : iconAssignments )
		{
			action->setIcon ( icons->icon ( iconName ) );
		}
	}

	NodeContextActions MainWindow::node_context_actions () const
	{
		NodeContextActions actions;

		actions.cut         = actionCut;
		actions.copy        = actionCopy;
		actions.paste       = actionPaste;
		actions.copyPointer = actionCopyPointer;
		actions.duplicate = actionDuplicateNode;
		actions.remove    = actionDeleteNode;
		actions.rename    = actionRenameKey;
		actions.moveUp    = actionMoveUp;
		actions.moveDown  = actionMoveDown;

		actions.objectsToArray = actionObjectsToArray;
		actions.arrayToObjects = actionArrayToObjects;
		actions.normalizeArray = actionNormalizeArray;

		// EDIT-04's explicit placement pair. The predicates are evaluated when a context menu opens, so they read the
		// live selection -- a const method can hand out these captures because they close over MainWindow, not over a
		// snapshot.

		MainWindow* const self = const_cast<MainWindow*> ( this );

		actions.addChild      = [ self ] ( JsonKind kind ) { self->add_child   ( kind ); };
		actions.addSibling    = [ self ] ( JsonKind kind ) { self->add_sibling ( kind ); };
		actions.canAddChild   = [ self ] () { return self->can_add_child   (); };
		actions.canAddSibling = [ self ] () { return self->can_add_sibling (); };

		return actions;
	}

	void MainWindow::create_workspace ()
	{
		// Two card-surface panes (STYLE-01/02/05) divided by a vertical splitter, each drawn by views/Card: an opaque
		// surface, an 8 px radius and a one-pixel border. Painted rather than styled in QSS, because a stylesheet here
		// would reach every locked surface in the subtree -- see Card.hpp.

		// The editor card carries two widgets, not one -- the find bar sits INSIDE it, above the tab strip (FIND-01) --
		// so the card takes a list.

		auto make_card = [ this ] ( std::initializer_list<QWidget*> contents ) -> Card*
		{
			Card* card = new Card ( this );

			for ( QWidget* const content : contents )
			{
				card->add_content ( content );
			}

			return card;
		};

		// Both panes are live from Phase 7. They are not wired to each other -- the SelectionService is the only thing
		// between them, which is what lets either be tested without the other.

		treePane   = new TreeViewPane ( document, selection, icons, this );
		editorPane = new EditorPane   ( document, selection, icons, this );

		// The registered editor views (EDITOR-01). Registration order is the tab-strip order,
		// and each view is exactly one provider and one line -- which is the whole claim the seam was built to make.

		editorPane->register_view
		(
			std::make_unique<FormViewProvider> ( document, undo, selection, settings, status, clipboard, node_context_actions () )
		);

		editorPane->register_view ( std::make_unique<TextViewProvider> ( document, settings ) );

		editorPane->register_view ( std::make_unique<CodeViewProvider> ( document, undo, settings, status, selection ) );

		// Tab / Shift+Tab move the keyboard between the two panes (NAV-04). MainWindow owns the ring because it is the
		// only thing that knows both panes; each pane still decides where its own landing spot is.

		paneCycler = new PaneCycler ( this );

		paneCycler->add_pane ( treePane,   [ this ] () { treePane->take_focus (); } );

		paneCycler->add_pane
		(
			editorPane,
			[ this ] ()       { editorPane->take_focus (); },
			[ this ] () -> bool { return editorPane->active_view_claims_tab_key (); }
		);

		// The find bar (FIND-01..03), hidden until Ctrl+F. It lives in the editor CARD rather than in the editor pane so
		// that it is outside the two panes PaneCycler knows about: Tab inside the bar is then ordinary control-to-control
		// navigation and never jumps to a pane mid-query, and Tab from the workspace never lands on it (NAV-04 / NAV-05).

		findBar = new FindBar ( findController, icons, this );

		treeCard   = make_card ( { treePane } );
		editorCard = make_card ( { findBar, editorPane } );

		// SET-03. Pushed in rather than read by the cards, which know nothing about settings (Card.hpp), and applied
		// here at construction so a stored No is honoured on the very first paint rather than after a repaint.

		apply_pane_corner_setting ();

		treeCard->setMinimumWidth ( config::workspace::MINIMUM_TREE_PANE_WIDTH );

		workspaceSplitter = new WorkspaceSplitter ( Qt::Horizontal, this );
		workspaceSplitter->setObjectName ( QStringLiteral ( "workspaceSplitter" ) );
		workspaceSplitter->setChildrenCollapsible ( false );
		workspaceSplitter->setHandleWidth ( config::workspace::SPLITTER_HANDLE_WIDTH );
		workspaceSplitter->addWidget ( treeCard );
		workspaceSplitter->addWidget ( editorCard );
		workspaceSplitter->setStretchFactor ( 0, 0 );
		workspaceSplitter->setStretchFactor ( 1, 1 );
		workspaceSplitter->setSizes
		(
			{ config::workspace::INITIAL_TREE_PANE_WIDTH, config::workspace::INITIAL_EDITOR_PANE_WIDTH }
		);

		// A consistent margin frames the pair (STYLE-03, 4 px grid).

		QWidget*     workspace       = new QWidget ( this );
		QVBoxLayout* workspaceLayout = new QVBoxLayout ( workspace );

		workspaceLayout->setContentsMargins
		(
			config::workspace::CONTENT_MARGIN, config::workspace::CONTENT_MARGIN,
			config::workspace::CONTENT_MARGIN, config::workspace::CONTENT_MARGIN
		);

		workspaceLayout->addWidget ( workspaceSplitter );

		setCentralWidget ( workspace );
	}

	void MainWindow::create_status_bar ()
	{
		// Permanent panes on the right; the transient message area is the status bar's own
		// left-hand region, fed by StatusService. Caret stays empty until the Code View (Phase 8).

		auto add_separator = [ this ] ()
		{
			QFrame* separator = new QFrame ( this );
			separator->setFrameShape ( QFrame::VLine );
			separator->setFrameShadow ( QFrame::Sunken );

			statusBar ()->addPermanentWidget ( separator );
		};

		statusDocumentLabel = new QLabel ( this );
		statusNodePathLabel = new QLabel ( this );
		statusNodeInfoLabel = new QLabel ( this );
		statusCaretLabel    = new QLabel ( this );

		statusBar ()->addPermanentWidget ( statusDocumentLabel );
		add_separator ();
		statusBar ()->addPermanentWidget ( statusNodePathLabel );
		add_separator ();
		statusBar ()->addPermanentWidget ( statusNodeInfoLabel );
		add_separator ();
		statusBar ()->addPermanentWidget ( statusCaretLabel );
	}

	void MainWindow::wire_services ()
	{
		// Document -> title / status.

		connect ( document, &JsonDocument::reset,             this, &MainWindow::update_title );
		connect ( document, &JsonDocument::reset,             this, &MainWindow::update_status_document );
		connect ( document, &JsonDocument::reset,             this, &MainWindow::update_status_selection );
		connect ( document, &JsonDocument::dirty_changed,     this, [ this ] ( bool ) { update_title (); update_status_document (); } );
		connect ( document, &JsonDocument::file_path_changed, this, [ this ] ( const QString& ) { update_title (); update_status_document (); } );

		// Selection -> status and command enablement (the commands act on the selection, so their enabled state tracks
		// it -- disabled-not-hidden).

		connect ( selection, &SelectionService::selection_changed, this, [ this ] ( const JsonPointer&, SelectionOrigin ) { update_status_selection (); update_command_enablement (); } );
		connect ( selection, &SelectionService::selection_cleared, this, [ this ] () { update_status_selection (); update_command_enablement (); } );

		// An edit can change what the selected node is (a delete, a type change) and always changes the undo stack, so
		// the enablement is recomputed after every document mutation and every stack move.

		connect ( document,     &JsonDocument::node_changed,   this, [ this ] ( const JsonPointer&, DocumentChange ) { update_command_enablement (); } );
		connect ( undo->stack (), &QUndoStack::indexChanged,   this, [ this ] ( int ) { update_command_enablement (); } );

		// Keyboard focus decides where Cut / Copy / Paste act (a text editor, the array table, or the tree), and the
		// clipboard's contents decide whether Paste applies at all. These two move far more often than the document
		// (every editor open / close, every copy anywhere in the OS), so they recompute only the clipboard slice.

		connect ( qApp,      &QApplication::focusChanged,        this, [ this ] ( QWidget*, QWidget* ) { update_clipboard_enablement (); } );
		connect ( clipboard, &ClipboardService::content_changed, this, &MainWindow::update_clipboard_enablement );

		// -- File lifecycle (Phase 10) --------------------------------------------------------------------------------

		// The controller's one piece of UI knowledge, injected rather than known: may it leave the active view without
		// dropping an uncommitted edit (EDITOR-09)? A lambda, so the pane need not exist when the controller is built.

		fileController->set_view_departure_gate ( [ this ] ()
		{
			return ( editorPane == nullptr ) || editorPane->confirm_leaving_active_view ();
		} );

		connect ( fileController, &FileController::recent_files_changed, this, &MainWindow::rebuild_recent_files_menu );

		// -- Printing (Phase 13) --------------------------------------------------------------------------------------
		//
		// The controller's only tie to the UI, injected the same way and for the same reason as the departure gate
		// above: it prints the ACTIVE VIEW's rendering (FILE-12), which is a question only the editor pane can answer,
		// and a lambda means the pane need not exist when the controller is built.

		printController->set_content_source ( [ this ] ( int availableColumns )
		{
			return ( editorPane != nullptr ) ? editorPane->print_content ( availableColumns ) : PrintContent ();
		} );

		// -- Find and Go To (Phase 11) --------------------------------------------------------------------------------
		//
		// The window owns none of the search behaviour: Ctrl+F shows the bar, F3 / Shift+F3 step the controller, and
		// Ctrl+G runs the dialog. Everything a match does downstream -- the tree revealing it, the editor pane
		// presenting it, the status bar naming it -- follows from the SelectionService write the controller makes.

		connect ( actionFind,         &QAction::triggered, this, [ this ] () { findBar->open (); } );
		connect ( actionGoTo,         &QAction::triggered, this, &MainWindow::show_go_to_dialog );
		connect ( actionFindNext,     &QAction::triggered, this, [ this ] () { findController->find_next (); } );
		connect ( actionFindPrevious, &QAction::triggered, this, [ this ] () { findController->find_previous (); } );
		connect ( actionCopyPointer,  &QAction::triggered, this, [ this ] () { findController->copy_selection_pointer (); } );

		// Where the keyboard goes when the bar is dismissed. The bar remembers where it came FROM; the window decides
		// what to do when that widget is gone or hidden -- a Code View tab rebuilt under an open bar is exactly that
		// case -- and the tree is the answer, since Find navigates the tree.

		connect ( findBar, &FindBar::dismissed, this, [ this ] ()
		{
			QWidget* const origin = findBar->focus_origin ();

			if ( ( origin != nullptr ) && origin->isVisible () && ( origin->focusPolicy () != Qt::NoFocus ) )
			{
				origin->setFocus ( Qt::OtherFocusReason );
			}
			else
			{
				treePane->take_focus ();
			}
		} );

		// SET-09: a configured log folder that cannot be written falls back to the default, silently in the log and once
		// to the user.

		connect ( diagnostics, &DiagnosticLog::fell_back_to_default_folder, this, [ this ] ( const QString& configuredFolder )
		{
			dialogs->show_information
			(
				tr ( "Diagnostic Logging" ),
				tr ( "The log folder \"%1\" could not be written to.\n\nLogging to \"%2\" instead." )
					.arg ( configuredFolder, DiagnosticLog::platform_default_folder () )
			);
		} );

		// SET-04: a toolbar layout change takes effect at once, without a restart. Every other setting reaches its
		// reader the same way -- through the store's change signal -- which is why the Settings dialog itself needs to
		// know nothing about who consumes what.

		connect ( settings, &SettingsStore::changed, this, [ this ] ( const QString& key )
		{
			if ( key == settings_keys::TOOLBAR_LAYOUT )
			{
				apply_toolbar_layout ();
			}

			// SET-05 is an input to Rename Key's enablement, and it is the one input that does not already recompute the
			// command surface -- selection, document, undo stack, focus and clipboard all do.

			if ( key == settings_keys::FORM_ALLOW_KEY_EDITING )
			{
				update_command_enablement ();
			}

			// SET-03's pane corners. The cards are not settings-aware, so the window is what carries the change to
			// them -- and it takes effect on the next repaint, with no restart and nothing to rebuild.

			if ( key == settings_keys::ROUNDED_PANE_CORNERS )
			{
				apply_pane_corner_setting ();
			}
		} );

		// Status service -> the status bar's transient message area, and -- when diagnostic logging is on -- the log
		// (SET-09).
		//
		// Every outcome worth recording already passes through here: the file commands' results, the import and export
		// outcomes, and every validation refusal (VAL-04). So one connection gives the log a genuinely useful trace, and
		// no call site has to remember to write to it. The log itself is a no-op while the setting is off.

		connect ( status, &StatusService::message_posted,  this, [ this ] ( const QString& text, int timeout )
		{
			statusBar ()->showMessage ( text, timeout );

			diagnostics->write ( text );
		} );
		connect ( status, &StatusService::message_cleared, this, [ this ] () { statusBar ()->clearMessage (); } );

		// -- Tree pane (Phase 6) --------------------------------------------------------------------------------------

		// Expand All / Collapse All are tree commands, so they go live with the tree (TREE-05). They are the first two
		// actions to leave the present-but-disabled set Phase 5 established.

		connect ( actionExpandAll,       &QAction::triggered, treePane, &TreeViewPane::expand_all );
		connect ( actionCollapseAll,     &QAction::triggered, treePane, &TreeViewPane::collapse_all );
		connect ( actionWidenTreePane,  &QAction::triggered, this, [ this ] () { nudge_workspace_splitter (  config::workspace::KEYBOARD_RESIZE_STEP ); } );
		connect ( actionNarrowTreePane, &QAction::triggered, this, [ this ] () { nudge_workspace_splitter ( -config::workspace::KEYBOARD_RESIZE_STEP ); } );

		connect ( actionExpandSubtree,   &QAction::triggered, treePane, &TreeViewPane::expand_current_subtree );
		connect ( actionCollapseSubtree, &QAction::triggered, treePane, &TreeViewPane::collapse_current_subtree );

		connect ( document, &JsonDocument::reset, this, &MainWindow::update_command_enablement );

		// The context menu shares the menu bar's QActions, so the two are enabled and disabled together (TREE-06). The
		// commands themselves are still Phase 9's -- what is wired here is the menu structure and the tree-scoped
		// shortcut context.

		treePane->set_context_actions ( node_context_actions () );

		// The tree's own four (TREE-05 / TREE-06). Handed over separately from the node commands because the Form View
		// shares those and these are the tree's alone -- both surfaces then carry the SAME actions, so a label, an icon,
		// or an enabled state cannot say one thing in the View menu and another in the context menu.

		TreeViewCommands viewCommands;

		viewCommands.expandAll       = actionExpandAll;
		viewCommands.collapseAll     = actionCollapseAll;
		viewCommands.expandSubtree   = actionExpandSubtree;
		viewCommands.collapseSubtree = actionCollapseSubtree;

		treePane->set_view_commands ( viewCommands );

		// Scope the EDIT-04 accelerators to the tree (Qt::WidgetWithChildrenShortcut), so Insert / Ctrl+Insert reach the
		// Add Child / Add Sibling chooser only while the tree has the keyboard -- never stealing Insert from a text
		// editor elsewhere in the window (section 4).

		for ( QAction* accelerator : { actionAddChildAccelerator, actionAddSiblingAccelerator } )
		{
			accelerator->setShortcutContext ( Qt::WidgetWithChildrenShortcut );

			treePane->view ()->addAction ( accelerator );
		}

		// -- Editor pane (Phase 7) ------------------------------------------------------------------------------------

		// The Form View was given the same action set at registration (EDITOR-02 offers this menu on a key label), so
		// the tree's menu and the form's are enabled and disabled together.
		//
		// The tree's two GESTURE channels, both distinct from its selection (EDITOR-04). Selection alone is passive --
		// it is what a Down arrow produces, and it must leave the keyboard in the tree -- so the editing caret changes
		// hands only on one of these.
		//
		//   node_clicked   -- a single click. The view decides whether that is its activation gesture ("Edit on").
		//   node_activated -- Enter or a double-click, which always is.

		connect ( treePane, &TreeViewPane::node_clicked, this, [ this ] ( const JsonPointer& )
		{
			editorPane->tree_node_clicked ();
		} );

		connect ( treePane, &TreeViewPane::node_activated, this, [ this ] ( const JsonPointer& )
		{
			editorPane->activate_editing ();
		} );

		// Theme changes (e.g. later via the Settings dialog) keep the View > Theme radio set in sync.

		connect ( theme, &ThemeService::theme_changed, this, [ this ] ( Theme ) { sync_theme_actions (); } );

		// A re-tint invalidates every icon handed out, so the actions re-fetch theirs. This covers
		// the OS-driven repaint under the System theme as well, which never emits theme_changed.

		if ( icons != nullptr )
		{
			connect ( icons, &IconLibrary::icons_changed, this, &MainWindow::apply_action_icons );
		}
	}

	void MainWindow::restore_geometry ()
	{
		// Persisted geometry wins; 1024x768 is the first-run default (NFR-06).

		const QByteArray geometry = settings->value_bytes ( settings_keys::WINDOW_GEOMETRY );

		if ( geometry.isEmpty () || !restoreGeometry ( geometry ) )
		{
			resize ( config::window::DEFAULT_WIDTH, config::window::DEFAULT_HEIGHT );
		}

		const QByteArray windowState = settings->value_bytes ( settings_keys::WINDOW_STATE );

		if ( !windowState.isEmpty () )
		{
			restoreState ( windowState );

			// restoreState carries the toolbar's VISIBILITY, and it runs after create_toolbar -- so a settings file that
			// remembers a visible bar outranks the SET-04 rule that an empty layout has no bar, and brings back an empty
			// strip. The same shape as lesson Q13: persisted state silently outranking the code. Re-asserted from the
			// bar's own content, which is the rule stated where it cannot drift from apply_toolbar_layout's.

			mainToolBar->setVisible ( !mainToolBar->actions ().isEmpty () );
		}

		restore_splitter_sizes ();
	}

	void MainWindow::restore_splitter_sizes ()
	{
		// ONLY the pane widths. QSplitter::restoreState would also restore the handle width, the collapsible flag and
		// the resize mode -- all of them design constants this class sets a few lines earlier, and all of them
		// overwritten by whatever was saved. That is not hypothetical: it is why the splitter kept its old 6 px handle
		// after the constant became 8, on every launch, for anyone with an existing settings file.

		// Clear the superseded whole-state value, so an upgraded settings file does not keep a copy of design constants
		// that no longer mean anything.

		settings->remove ( settings_keys::LEGACY_SPLITTER_STATE );

		const QStringList sizeTexts = settings->value_string_list ( settings_keys::SPLITTER_SIZES );

		if ( sizeTexts.size () != workspaceSplitter->count () )
		{
			return;
		}

		QList<int> sizes;

		for ( const QString& sizeText : sizeTexts )
		{
			bool isNumber = false;

			const int size = sizeText.toInt ( &isNumber );

			// A malformed or negative entry leaves the whole thing alone rather than half-applying it -- the first-run
			// division is a better answer than a pane of unexplained width.

			if ( !isNumber || ( size < 0 ) )
			{
				return;
			}

			sizes.append ( size );
		}

		workspaceSplitter->setSizes ( sizes );
	}

	void MainWindow::persist_geometry ()
	{
		settings->set_bytes ( settings_keys::WINDOW_GEOMETRY, saveGeometry () );
		settings->set_bytes ( settings_keys::WINDOW_STATE,    saveState () );
		// Widths only -- see restore_splitter_sizes for why the whole state is not persisted.

		QStringList sizeTexts;

		for ( const int size : workspaceSplitter->sizes () )
		{
			sizeTexts.append ( QString::number ( size ) );
		}

		settings->set_string_list ( settings_keys::SPLITTER_SIZES, sizeTexts );
	}

	//=================================================================================================================
	// Update Helpers
	//=================================================================================================================

	void MainWindow::update_title ()
	{
		// Caption: "<document name>[*] - VJE"; just "VJE" with no document. The [*] placeholder is
		// resolved by Qt from windowModified.

		if ( !document->has_root () )
		{
			setWindowTitle ( application_name () );
			setWindowModified ( false );

			return;
		}

		setWindowTitle ( tr ( "%1[*] - %2" ).arg ( document_display_name ( *document ), application_name () ) );
		setWindowModified ( document->is_dirty () );
	}

	void MainWindow::update_status_document ()
	{
		if ( !document->has_root () )
		{
			statusDocumentLabel->setText ( tr ( "No document" ) );

			return;
		}

		const QString modifiedMark = document->is_dirty () ? QStringLiteral ( " *" ) : QString ();

		statusDocumentLabel->setText ( document_display_name ( *document ) + modifiedMark );
	}

	void MainWindow::update_status_selection ()
	{
		// Resolve the current selection against the live document for the node-path / node-info panes.

		JsonNode* node = nullptr;

		if ( selection->has_selection () && document->has_root () )
		{
			node = document->resolve ( selection->selection () );
		}

		if ( node == nullptr )
		{
			statusNodePathLabel->clear ();
			statusNodeInfoLabel->clear ();

			return;
		}

		const QString pointerText = selection->selection ().to_string ();

		statusNodePathLabel->setText ( pointerText.isEmpty () ? QStringLiteral ( "/" ) : pointerText );
		statusNodeInfoLabel->setText ( describe_node ( *node ) );
	}

	void MainWindow::update_command_enablement ()
	{
		// ONE place computes the enabled state of every command Phase 9 owns, from the current selection, document,
		// undo stack, keyboard focus, and clipboard (disabled-not-hidden, section 2.3). It is called whenever any of
		// those change.

		const bool      hasDocument    = document->has_root ();
		JsonNode* const node           = selected_node ();
		JsonNode* const parent         = ( node != nullptr ) ? node->parent () : nullptr;
		const bool      isRoot         = ( node != nullptr ) && ( parent == nullptr );
		const bool      parentIsObject = ( parent != nullptr ) && ( parent->kind () == JsonKind::Object );

		// -- File: the lifecycle commands (FILE-01..03) --------------------------------------------------------------
		//
		// New and Open are always available (each gates the current document itself). The other three need a document.
		//
		// Save is deliberately NOT gated on the dirty flag. The bytes a save writes are the document rendered through the
		// format profile, so changing an SET-07 setting -- indentation, brace style -- changes them without touching the
		// document; a Save disabled on a clean document would leave the user unable to rewrite the file in the format
		// they just chose (FILE-03).

		actionClose ->setEnabled ( hasDocument );
		actionSave  ->setEnabled ( hasDocument );
		actionSaveAs->setEnabled ( hasDocument );

		// -- File: printing (FILE-12) --------------------------------------------------------------------------------
		//
		// A document and nothing finer, for the reason the exports settled on that in Phase 12.5: what a print would
		// actually produce is the ACTIVE VIEW's rendering, which changes with the tab as well as with the selection, so
		// an enablement fine enough to be honest about it would flicker as the user moved between tabs -- and a
		// disabled command still could not say why. The command explains itself instead when it declines.
		//
		// Page Setup is gated the same way rather than being always-on. It configures the printer that File > Print
		// will use, so offering it while there is nothing to print would be offering the user the settings for a
		// command they cannot reach.

		actionPageSetup->setEnabled ( hasDocument );
		actionPrint    ->setEnabled ( hasDocument );

		// -- File: Export (FILE-11). Enabled on a DOCUMENT and nothing finer, for all three formats alike.
		//
		//    The per-selection preconditions (CSV's) are deliberately NOT enablement any more. A disabled Export CSV
		//    told the user nothing about which of four things was wrong with their selection, and a command that
		//    silently is not there is indistinguishable from one that is broken. It is offered, and it explains itself
		//    when it declines -- describe_export_blocker, raised by FileController::export_document.
		//
		//    The tooltip carries the same sentence in advance, so the reason is available on hover as well as on
		//    invocation; it is the converter table's wording in both places, never a second opinion formed here.

		for ( QAction* const exportAction : exportActions )
		{
			const ConverterFormat* const format = find_converter ( exportAction->data ().toString () );

			if ( format == nullptr )
			{
				exportAction->setEnabled ( false );

				continue;
			}

			const ExportSource source = fileController->export_source ( *format );

			exportAction->setEnabled ( source.blocker != ExportBlocker::NoDocument );
			exportAction->setToolTip ( describe_export_blocker ( *format, source.blocker ) );
		}

		// -- Edit: Find and Go To (FIND-01..04) ---------------------------------------------------------------------
		//
		// All four need a document and nothing else. F3 / Shift+F3 are deliberately NOT gated on there being a query:
		// with no query they are a no-op that reports nothing, which is the right answer to a key pressed by habit --
		// and disabling them would make the shortcut's availability depend on the contents of a hidden text field.

		actionFind        ->setEnabled ( hasDocument );
		actionGoTo        ->setEnabled ( hasDocument );
		actionFindNext    ->setEnabled ( hasDocument );
		actionFindPrevious->setEnabled ( hasDocument );

		// FIND-05 needs a SELECTION rather than a document: what it copies is the selected node's pointer, and there is
		// no sensible answer without one. (The root counts -- its pointer is the empty string, which is what Go To's
		// empty field means, so the round trip holds and the status bar says so.)

		actionCopyPointer->setEnabled ( node != nullptr );

		// -- View: Expand / Collapse (tree commands, TREE-05) -------------------------------------------------------
		//
		// The whole-tree pair needs only a document. The subtree pair acts on the tree's CURRENT ROW, so it asks the
		// pane about that row rather than about the selection -- the two can legitimately differ, because a no-reveal
		// selection deliberately leaves the current row where it is (EDITOR-04).

		actionExpandAll  ->setEnabled ( hasDocument );
		actionCollapseAll->setEnabled ( hasDocument );

		const bool currentRowIsBranch = ( treePane != nullptr ) && treePane->current_row_is_branch ();

		actionExpandSubtree  ->setEnabled ( currentRowIsBranch );
		actionCollapseSubtree->setEnabled ( currentRowIsBranch );

		// -- Edit: Undo / Redo (UNDO-01..03) ------------------------------------------------------------------------

		actionUndo->setEnabled ( undo->can_undo () );
		actionRedo->setEnabled ( undo->can_redo () );

		// -- Document: Add (EDIT-03). A container takes the new node as a child; a scalar takes it as a sibling, which
		//    needs a parent -- so a lone scalar root cannot add either way.

		const bool canAdd = ( node != nullptr ) && ( node->is_container () || ( parent != nullptr ) );

		for ( QAction* addAction : { actionAddObject, actionAddArray, actionAddString, actionAddNumber, actionAddBoolean, actionAddNull } )
		{
			addAction->setEnabled ( canAdd );
		}

		// The EDIT-04 accelerators (Insert / Ctrl+Insert): a child needs a container, a sibling needs a parent. A
		// disabled shortcut does not fire, so this is also what keeps Insert free for a text editor when the tree cannot
		// take an add.

		actionAddChildAccelerator  ->setEnabled ( can_add_child () );
		actionAddSiblingAccelerator->setEnabled ( can_add_sibling () );

		// -- Document: node operations (EDIT-02, 05, 07, 08) --------------------------------------------------------

		// Rename applies to an object member whose key is not duplicated (a pointer names the first of a duplicate, so
		// the second is not renameable in place -- EDIT-02).

		// SET-05 gates the command as well as the Form View's key column. Both routes ask key_editing_allowed(), because
		// the Form View offers this very action on the context menu it shares with the tree -- gating only the cell
		// would leave Rename Key renaming from inside the pane whose key column had just refused to open.

		const bool renameable = parentIsObject
		                     && key_editing_allowed ( settings )
		                     && ( parent->key_count ( parent->member_key ( node->index_in_parent () ) ) < 2 );

		actionRenameKey    ->setEnabled ( renameable );
		actionDuplicateNode->setEnabled ( ( node != nullptr ) && !isRoot );
		actionDeleteNode   ->setEnabled ( ( node != nullptr ) && !isRoot );

		// Move is disabled at the edges rather than a silent no-op.

		const int  index        = ( parent != nullptr ) ? node->index_in_parent () : -1;
		const int  siblingCount = ( parent != nullptr )
		                        ? ( ( parent->kind () == JsonKind::Object ) ? parent->member_count () : parent->array_size () )
		                        : 0;

		actionMoveUp  ->setEnabled ( ( parent != nullptr ) && ( index > 0 ) );
		actionMoveDown->setEnabled ( ( parent != nullptr ) && ( index >= 0 ) && ( index < siblingCount - 1 ) );

		// -- Document: array / object transforms (EDIT-11..13) ------------------------------------------------------

		const bool isArray  = ( node != nullptr ) && ( node->kind () == JsonKind::Array );
		const bool isObject = ( node != nullptr ) && ( node->kind () == JsonKind::Object );

		bool allElementsObjects = isArray;

		if ( isArray )
		{
			for ( int elementIndex = 0; elementIndex < node->array_size (); ++elementIndex )
			{
				if ( node->array_element ( elementIndex )->kind () != JsonKind::Object )
				{
					allElementsObjects = false;

					break;
				}
			}
		}

		actionNormalizeArray->setEnabled ( isArray && ( node->array_size () > 0 ) && allElementsObjects );
		actionArrayToObjects->setEnabled ( isArray );
		actionObjectsToArray->setEnabled ( isObject );

		// -- Edit: Cut / Copy / Paste (EDIT-06, EDITOR-11) ----------------------------------------------------------

		update_clipboard_enablement ();
	}

	void MainWindow::update_clipboard_enablement ()
	{
		// The Cut / Copy / Paste routing slice, split from update_command_enablement because its inputs move far more
		// often than the rest's: keyboard focus changes on every editor open / close and pane cycle, and the
		// clipboard on every copy anywhere in the OS. Wiring those two triggers here alone keeps them away from the
		// document-shaped work -- the normalize-array enablement walks every element of a selected array, which is
		// not focus-change money (NFR-03). Routed by keyboard focus (section 2.3); the full pass ends by calling
		// this, so it still covers everything.

		JsonNode* const node   = selected_node ();
		JsonNode* const parent = ( node != nullptr ) ? node->parent () : nullptr;
		const bool      isRoot = ( node != nullptr ) && ( parent == nullptr );

		QWidget* const focusWidget    = QApplication::focusWidget ();
		const bool     inComboEditor  = ( qobject_cast<QComboBox*> ( focusWidget ) != nullptr );
		const bool     inTextEditor   = ( qobject_cast<QLineEdit*> ( focusWidget ) != nullptr )
		                             || ( qobject_cast<QPlainTextEdit*> ( focusWidget ) != nullptr )
		                             || ( qobject_cast<QTextEdit*> ( focusWidget ) != nullptr );
		const bool     cellActive     = editor_pane_holds_focus () && editorPane->cell_clipboard_active ();
		const bool     hasClipboard   = ( clipboard != nullptr ) && clipboard->has_content ();

		if ( inComboEditor )
		{
			// The boolean cell editor: a plain dropdown with no text to cut, copy, or paste into. Disabled, not
			// enabled-but-dead -- the menu should tell the truth about the state.

			actionCut  ->setEnabled ( false );
			actionCopy ->setEnabled ( false );
			actionPaste->setEnabled ( false );
		}
		else if ( inTextEditor )
		{
			// A text editor keeps its own clipboard behaviour; the menu items mirror it so the menu is never a dead end.

			actionCut  ->setEnabled ( true );
			actionCopy ->setEnabled ( true );
			actionPaste->setEnabled ( hasClipboard );
		}
		else if ( cellActive )
		{
			// The array table's current cell (EDITOR-11) -- only while the editor pane holds the keyboard. Section 2.3's
			// precedence: a tree-focused gesture acts on the NODE even when the table behind it shows a current cell.

			actionCut  ->setEnabled ( true );
			actionCopy ->setEnabled ( true );
			actionPaste->setEnabled ( hasClipboard );
		}
		else
		{
			// The tree node clipboard (EDIT-06): copy any node, cut / paste a non-root one.

			actionCopy ->setEnabled ( node != nullptr );
			actionCut  ->setEnabled ( ( node != nullptr ) && !isRoot );
			actionPaste->setEnabled ( ( node != nullptr ) && hasClipboard && ( node->is_container () || ( parent != nullptr ) ) );
		}
	}

	void MainWindow::rebuild_recent_files_menu ()
	{
		// FILE-05. The list is short (config::limits::RECENT_FILES) and only changes on an open or a save, so rebuilding
		// it wholesale is cheaper to reason about than patching it -- and it keeps the accelerator digits in step with the
		// order, which a patch would not.

		recentFilesMenu->clear ();

		const QStringList recentFiles = fileController->recent_files ();

		if ( recentFiles.isEmpty () )
		{
			// A disabled placeholder rather than an empty menu: an empty submenu opens onto nothing and reads as a fault.

			QAction* const placeholder = recentFilesMenu->addAction ( tr ( "(No recent files)" ) );

			placeholder->setEnabled ( false );

			return;
		}

		int position = 1;

		for ( const QString& path : recentFiles )
		{
			// "&1 name.json" -- the digit is the accelerator, and the full path is the tooltip, since the file name alone
			// is ambiguous across folders.

			QAction* const entry = recentFilesMenu->addAction ( tr ( "&%1 %2" ).arg ( position ).arg ( QFileInfo ( path ).fileName () ) );

			entry->setToolTip ( path );
			entry->setStatusTip ( path );

			connect ( entry, &QAction::triggered, this, [ this, path ] () { fileController->open_path ( path ); } );

			++position;
		}
	}

	void MainWindow::show_settings_dialog ()
	{
		// The schema carries every group but the Toolbar one, whose fields are these buttons (SET-04). Everything the
		// dialog changes is written to the store on OK, and reaches its readers -- the views, ThemeService, the toolbar,
		// DiagnosticLog -- through the store's change signal.

		SettingsDialog dialog ( settings_schema_with_toolbar ( toolBarCatalogue ), settings, theme, dialogs.get (), this );

		dialog.exec ();
	}

	void MainWindow::show_go_to_dialog ()
	{
		// Pre-filled with where the user already is, so the common case -- go somewhere NEAR here -- starts from a path
		// to edit rather than from an empty box. The dialog selects it, so typing still replaces it outright.

		const QString startingPointer = selection->has_selection () ? selection->selection ().to_string () : QString ();

		GoToDialog dialog ( findController, startingPointer, this );

		dialog.exec ();
	}

	void MainWindow::sync_theme_actions ()
	{
		switch ( theme->theme () )
		{
			case Theme::Light:  actionThemeLight ->setChecked ( true ); break;
			case Theme::Dark:   actionThemeDark  ->setChecked ( true ); break;
			case Theme::System: actionThemeSystem->setChecked ( true ); break;
		}
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	bool MainWindow::open_document_from_path ( const QString& path )
	{
		return fileController->open_path ( path );
	}

	//=================================================================================================================
	// Command surface (Phase 9)
	//=================================================================================================================

	JsonNode* MainWindow::selected_node () const
	{
		if ( !selection->has_selection () || !document->has_root () )
		{
			return nullptr;
		}

		return document->resolve ( selection->selection () );
	}

	//-- Document > Add (EDIT-03 / 04) ----------------------------------------------------------------------------------

	void MainWindow::add_node_auto ( JsonKind kind )
	{
		// EDIT-03 placement: a container receives the new node as its last child, a scalar as the sibling after itself.

		JsonNode* const node = selected_node ();

		if ( node == nullptr )
		{
			return;
		}

		if ( node->is_container () )
		{
			add_child ( kind );
		}
		else
		{
			add_sibling ( kind );
		}
	}

	bool MainWindow::can_add_child () const
	{
		JsonNode* const node = selected_node ();

		return ( node != nullptr ) && node->is_container ();
	}

	bool MainWindow::can_add_sibling () const
	{
		JsonNode* const node = selected_node ();

		return ( node != nullptr ) && ( node->parent () != nullptr );
	}

	void MainWindow::add_child ( JsonKind kind )
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || !node->is_container () )
		{
			return;
		}

		const JsonPointer selectionPointer = selection->selection ();

		QString key;

		if ( node->kind () == JsonKind::Object )
		{
			key = prompt_for_key ( *node, tr ( "Add Child" ) );

			if ( key.isNull () )
			{
				return;   // The user cancelled the key prompt.
			}
		}

		const int newIndex = ( node->kind () == JsonKind::Object ) ? node->member_count () : node->array_size ();

		if ( undo->add_child ( selectionPointer, kind, key ) != EditOutcome::Applied )
		{
			return;
		}

		// NAV-03: a tree-originated add selects (and reveals) the new node.

		const JsonPointer newPointer = ( node->kind () == JsonKind::Object )
		                             ? selectionPointer.child ( key )
		                             : selectionPointer.child ( QString::number ( newIndex ) );

		selection->set_selection ( newPointer, SelectionOrigin::Tree );
	}

	void MainWindow::add_sibling ( JsonKind kind )
	{
		JsonNode* const node = selected_node ();

		if ( node == nullptr )
		{
			return;
		}

		JsonNode* const parent = node->parent ();

		if ( parent == nullptr )
		{
			return;   // The root has no sibling.
		}

		const JsonPointer selectionPointer = selection->selection ();

		QString key;

		if ( parent->kind () == JsonKind::Object )
		{
			key = prompt_for_key ( *parent, tr ( "Add Sibling" ) );

			if ( key.isNull () )
			{
				return;
			}
		}

		const int newIndex = node->index_in_parent () + 1;

		if ( undo->add_sibling ( selectionPointer, kind, key ) != EditOutcome::Applied )
		{
			return;
		}

		const JsonPointer parentPointer = selectionPointer.parent ();

		const JsonPointer newPointer = ( parent->kind () == JsonKind::Object )
		                             ? parentPointer.child ( key )
		                             : parentPointer.child ( QString::number ( newIndex ) );

		selection->set_selection ( newPointer, SelectionOrigin::Tree );
	}

	void MainWindow::popup_add_child_menu ()
	{
		if ( !can_add_child () )
		{
			return;
		}

		// The keyboard counterpart of the tree context menu's Add Child submenu: the six typed adds, at the tree
		// selection (EDIT-04, section 4). Populated from the ONE add-type table (NodeContextActions), so the chooser
		// and the submenus cannot drift apart.

		popup_add_type_chooser ( [ this ] ( JsonKind kind ) { add_child ( kind ); } );
	}

	void MainWindow::popup_add_sibling_menu ()
	{
		if ( !can_add_sibling () )
		{
			return;
		}

		popup_add_type_chooser ( [ this ] ( JsonKind kind ) { add_sibling ( kind ); } );
	}

	void MainWindow::popup_add_type_chooser ( const std::function<void ( JsonKind )>& callback )
	{
		QMenu menu ( this );

		populate_add_type_menu ( &menu, callback );

		// A KEYBOARD gesture anchors at the keyboard's subject: beneath the tree's current row, not at the mouse,
		// which could be anywhere on screen. The row is on show in practice -- the accelerators are tree-scoped and
		// the view scrolls to its current row -- but a row without a rect falls back to the cursor rather than
		// popping at a stale corner.

		QTreeView* const view    = treePane->view ();
		const QRect      rowRect = view->visualRect ( view->currentIndex () );

		const QPoint anchor = rowRect.isValid ()
		                    ? view->viewport ()->mapToGlobal ( QPoint ( rowRect.left (), rowRect.bottom () + 1 ) )
		                    : QCursor::pos ();

		menu.exec ( anchor );
	}

	QString MainWindow::prompt_for_key ( const JsonNode& parentObject, const QString& title ) const
	{
		// EDIT-03: the user supplies an object member's key, duplicates rejected (VAL-02). Re-prompt on a clash so the
		// add flow is not lost; a cancel returns a NULL string (distinct from a legal empty key).

		QString suggestion = QStringLiteral ( "newKey" );

		while ( true )
		{
			bool accepted = false;

			const QString entered = QInputDialog::getText
			(
				const_cast<MainWindow*> ( this ), title, tr ( "Key:" ), QLineEdit::Normal, suggestion, &accepted
			);

			if ( !accepted )
			{
				return QString ();
			}

			if ( parentObject.has_member ( entered ) )
			{
				QMessageBox::warning ( const_cast<MainWindow*> ( this ), title, tr ( "That key already exists in this object." ) );

				suggestion = entered;

				continue;
			}

			return entered;
		}
	}

	//-- Document > node operations (EDIT-02, 05, 07, 08) and transforms (EDIT-11..13) --------------------------------

	void MainWindow::rename_key ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || ( node->parent () == nullptr ) || ( node->parent ()->kind () != JsonKind::Object ) )
		{
			return;
		}

		const QString currentKey = node->parent ()->member_key ( node->index_in_parent () );

		bool accepted = false;

		const QString newKey = QInputDialog::getText
		(
			this, tr ( "Rename Key" ), tr ( "Key:" ), QLineEdit::Normal, currentKey, &accepted
		);

		if ( !accepted )
		{
			return;
		}

		const JsonPointer pointer = selection->selection ();

		if ( undo->rename_key ( pointer, newKey ) == EditOutcome::Rejected )
		{
			QMessageBox::warning ( this, tr ( "Rename Key" ), tr ( "That key already exists in this object." ) );

			return;
		}

		// The member's pointer changed with its key, so re-select it at its new name (NAV-01).

		selection->set_selection ( pointer.parent ().child ( newKey ), SelectionOrigin::Tree );
	}

	void MainWindow::duplicate_node ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || ( node->parent () == nullptr ) )
		{
			return;
		}

		undo->duplicate_node ( selection->selection () );
	}

	void MainWindow::delete_node ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || ( node->parent () == nullptr ) )
		{
			return;
		}

		const JsonPointer pointer       = selection->selection ();
		const JsonPointer parentPointer = pointer.parent ();
		const int         removedIndex  = node->index_in_parent ();

		if ( undo->delete_node ( pointer ) == EditOutcome::Applied )
		{
			select_after_removal ( parentPointer, removedIndex );
		}
	}

	void MainWindow::move_node_up ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || ( node->parent () == nullptr ) )
		{
			return;
		}

		if ( undo->move_node ( selection->selection (), MoveDirection::Up ) == EditOutcome::Applied )
		{
			// The node kept its identity but changed position, so its pointer moved with it.

			selection->set_selection ( JsonPointer::from_node ( node ), SelectionOrigin::Tree );
		}
	}

	void MainWindow::move_node_down ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || ( node->parent () == nullptr ) )
		{
			return;
		}

		if ( undo->move_node ( selection->selection (), MoveDirection::Down ) == EditOutcome::Applied )
		{
			selection->set_selection ( JsonPointer::from_node ( node ), SelectionOrigin::Tree );
		}
	}

	void MainWindow::normalize_array ()
	{
		// The centralized enablement is the primary guard (array-only / object-only, disabled otherwise); the
		// selection re-check keeps these three safe standalone, like every sibling handler above.

		if ( selected_node () == nullptr )
		{
			return;
		}

		undo->normalize_array ( selection->selection () );
	}

	void MainWindow::array_to_objects ()
	{
		if ( selected_node () == nullptr )
		{
			return;
		}

		undo->array_to_objects ( selection->selection () );
	}

	void MainWindow::objects_to_array ()
	{
		if ( selected_node () == nullptr )
		{
			return;
		}

		undo->objects_to_array ( selection->selection () );
	}

	void MainWindow::select_after_removal ( const JsonPointer& parentPointer, int removedIndex )
	{
		// NAV-03: after a removal the selection moves to the following sibling, else the previous, else the parent.

		JsonNode* const parent = document->resolve ( parentPointer );

		if ( parent == nullptr )
		{
			selection->set_selection ( parentPointer, SelectionOrigin::Tree );

			return;
		}

		const int count = ( parent->kind () == JsonKind::Object ) ? parent->member_count () : parent->array_size ();

		if ( count == 0 )
		{
			selection->set_selection ( parentPointer, SelectionOrigin::Tree );

			return;
		}

		const int newIndex = std::min ( removedIndex, count - 1 );

		const JsonPointer newPointer = ( parent->kind () == JsonKind::Object )
		                             ? parentPointer.child ( parent->member_key ( newIndex ) )
		                             : parentPointer.child ( QString::number ( newIndex ) );

		selection->set_selection ( newPointer, SelectionOrigin::Tree );
	}

	//-- Edit > Cut / Copy / Paste (EDIT-06), routed by keyboard focus ------------------------------------------------

	bool MainWindow::editor_pane_holds_focus () const
	{
		// The gate on the CELL route (the 2026-07-24 review): the cell clipboard answers only while the editor pane
		// itself holds the keyboard. The array table keeps a current cell while the TREE has focus (a scalar tree
		// selection indicates its field), so gating on "a cell is current" alone would send a tree-focused Cut into
		// the cell -- nulling it -- instead of cutting the node. Section 2.3 routes by focus: the tree's gesture
		// belongs to the tree.

		QWidget* const focusWidget = QApplication::focusWidget ();

		return ( editorPane != nullptr ) && ( focusWidget != nullptr ) && editorPane->isAncestorOf ( focusWidget );
	}

	void MainWindow::handle_copy ()
	{
		QWidget* const focusWidget = QApplication::focusWidget ();

		if ( QLineEdit* const lineEdit = qobject_cast<QLineEdit*> ( focusWidget ) )
		{
			lineEdit->copy ();

			return;
		}

		if ( QPlainTextEdit* const plainTextEdit = qobject_cast<QPlainTextEdit*> ( focusWidget ) )
		{
			plainTextEdit->copy ();

			return;
		}

		if ( QTextEdit* const richTextEdit = qobject_cast<QTextEdit*> ( focusWidget ) )
		{
			richTextEdit->copy ();

			return;
		}

		if ( qobject_cast<QComboBox*> ( focusWidget ) != nullptr )
		{
			// The boolean cell editor has nothing to copy; the action is disabled in this state
			// (update_clipboard_enablement), so this return is the safety net.

			return;
		}

		if ( editor_pane_holds_focus () && editorPane->cell_copy () )
		{
			return;
		}

		copy_selected_node ();
	}

	void MainWindow::handle_cut ()
	{
		QWidget* const focusWidget = QApplication::focusWidget ();

		if ( QLineEdit* const lineEdit = qobject_cast<QLineEdit*> ( focusWidget ) )
		{
			lineEdit->cut ();

			return;
		}

		if ( QPlainTextEdit* const plainTextEdit = qobject_cast<QPlainTextEdit*> ( focusWidget ) )
		{
			plainTextEdit->cut ();

			return;
		}

		if ( QTextEdit* const richTextEdit = qobject_cast<QTextEdit*> ( focusWidget ) )
		{
			richTextEdit->cut ();

			return;
		}

		if ( qobject_cast<QComboBox*> ( focusWidget ) != nullptr )
		{
			return;   // As handle_copy: disabled in this state; the return is the safety net.
		}

		if ( editor_pane_holds_focus () && editorPane->cell_cut () )
		{
			return;
		}

		cut_selected_node ();
	}

	void MainWindow::handle_paste ()
	{
		QWidget* const focusWidget = QApplication::focusWidget ();

		if ( QLineEdit* const lineEdit = qobject_cast<QLineEdit*> ( focusWidget ) )
		{
			lineEdit->paste ();

			return;
		}

		if ( QPlainTextEdit* const plainTextEdit = qobject_cast<QPlainTextEdit*> ( focusWidget ) )
		{
			plainTextEdit->paste ();

			return;
		}

		if ( QTextEdit* const richTextEdit = qobject_cast<QTextEdit*> ( focusWidget ) )
		{
			richTextEdit->paste ();

			return;
		}

		if ( qobject_cast<QComboBox*> ( focusWidget ) != nullptr )
		{
			return;   // As handle_copy: disabled in this state; the return is the safety net.
		}

		if ( editor_pane_holds_focus () && editorPane->cell_paste () )
		{
			return;
		}

		paste_onto_selection ();
	}

	void MainWindow::copy_selected_node ()
	{
		JsonNode* const node = selected_node ();

		if ( node == nullptr )
		{
			return;
		}

		JsonNode* const parent    = node->parent ();
		const QString   sourceKey = ( ( parent != nullptr ) && ( parent->kind () == JsonKind::Object ) )
		                          ? parent->member_key ( node->index_in_parent () )
		                          : QString ();

		clipboard->copy_node ( *node, sourceKey );

		status->show_message ( tr ( "Copied node" ), 3000 );
	}

	void MainWindow::cut_selected_node ()
	{
		JsonNode* const node = selected_node ();

		if ( node == nullptr )
		{
			return;
		}

		JsonNode* const parent    = node->parent ();
		const QString   sourceKey = ( ( parent != nullptr ) && ( parent->kind () == JsonKind::Object ) )
		                          ? parent->member_key ( node->index_in_parent () )
		                          : QString ();

		clipboard->copy_node ( *node, sourceKey );

		// Cut is copy plus delete, as one undo step -- but the root cannot be deleted, so cutting it just copies.

		if ( parent == nullptr )
		{
			status->show_message ( tr ( "Copied node" ), 3000 );

			return;
		}

		const JsonPointer pointer       = selection->selection ();
		const JsonPointer parentPointer = pointer.parent ();
		const int         removedIndex  = node->index_in_parent ();

		if ( undo->delete_node ( pointer ) == EditOutcome::Applied )
		{
			select_after_removal ( parentPointer, removedIndex );

			status->show_message ( tr ( "Cut node" ), 3000 );
		}
		else
		{
			// The copy half still happened; say so rather than claiming a cut that did not.

			status->show_message ( tr ( "Copied node" ), 3000 );
		}
	}

	void MainWindow::paste_onto_selection ()
	{
		JsonNode* const node = selected_node ();

		if ( ( node == nullptr ) || !clipboard->has_content () )
		{
			return;
		}

		std::unique_ptr<JsonNode> value = clipboard->value ();

		if ( value == nullptr )
		{
			return;
		}

		if ( undo->paste_node ( selection->selection (), std::move ( value ), clipboard->source_key () ) == EditOutcome::Rejected )
		{
			QMessageBox::warning ( this, tr ( "Paste" ), tr ( "The clipboard content cannot be pasted here." ) );

			return;
		}

		status->show_message ( tr ( "Pasted node" ), 3000 );
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void MainWindow::closeEvent ( QCloseEvent* event )
	{
		// The two gates, in the fixed order (FileController.hpp): EDITOR-09 first, so an uncommitted Code View edit has
		// reached the document before FILE-08 asks whether to save it -- otherwise a Save on the way out would write the
		// document as it was before that edit. Both run BEFORE the geometry is persisted, since an aborted exit is not an
		// exit.

		if ( ( editorPane != nullptr ) && !editorPane->confirm_leaving_active_view () )
		{
			event->ignore ();

			return;
		}

		if ( !fileController->confirm_discard_changes () )
		{
			event->ignore ();

			return;
		}

		persist_geometry ();

		diagnostics->write ( tr ( "Session ended." ) );

		event->accept ();
	}

	void MainWindow::dragEnterEvent ( QDragEnterEvent* event )
	{
		// FILE-09. Accept only a LOCAL file: a dragged URL from a browser carries text/uri-list too, and there is nothing
		// this window can do with a remote one.

		if ( !first_dropped_local_file ( event->mimeData () ).isEmpty () )
		{
			event->acceptProposedAction ();
		}
	}

	void MainWindow::dropEvent ( QDropEvent* event )
	{
		const QString path = first_dropped_local_file ( event->mimeData () );

		if ( path.isEmpty () )
		{
			return;
		}

		event->acceptProposedAction ();

		// Through the ordinary Open pipeline, gates and all -- a drop is not a shortcut past the unsaved-changes question.
		// Deferred to the event loop so the drag source is released before a modal prompt appears on top of it.

		QMetaObject::invokeMethod ( this, [ this, path ] () { fileController->open_path ( path ); }, Qt::QueuedConnection );
	}
}

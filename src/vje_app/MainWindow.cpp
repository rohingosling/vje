//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   MainWindow implementation -- builds the shell (menus, toolbar, workspace splitter, status bar), wires the
//   shell-level commands live (theme, exit, about, command-line open), and persists window/splitter geometry. See the
//   header for the Phase 5 scope note: the remaining commands are present-but-disabled placeholders owned by later
//   phases.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "MainWindow.hpp"

#include "AppConfig.hpp"
#include "services/IconLibrary.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"
#include "views/CodeView.hpp"
#include "views/EditorPane.hpp"
#include "views/FormView.hpp"
#include "views/PaneCycler.hpp"
#include "views/TextView.hpp"
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
#include <QFileInfo>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>

#include <memory>
#include <QWidget>

#include <memory>

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
	{
		create_actions ();
		create_menus ();
		create_toolbar ();
		create_workspace ();
		create_status_bar ();
		wire_services ();

		apply_action_icons ();

		// Reflect the (initially empty) document and theme state before restoring the persisted geometry.

		update_title ();
		update_status_document ();
		update_status_selection ();
		update_tree_command_enablement ();
		sync_theme_actions ();

		restore_geometry ();
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

		// -- View ---------------------------------------------------------------------------------------------------

		actionExpandAll   = new QAction ( tr ( "&Expand All" ),   this );
		actionCollapseAll = new QAction ( tr ( "&Collapse All" ), this );

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

		// Everything below the shell level is disabled until its owning phase (tree/editor/command surface/file
		// lifecycle) wires a handler and its centralized enablement.

		const QList<QAction*> deferredActions =
		{
			actionNew, actionOpen, actionClose, actionSave, actionSaveAs, actionPageSetup, actionPrint, actionSettings,
			actionFind, actionGoTo, actionUndo, actionRedo, actionCut, actionCopy, actionPaste,
			actionAddObject, actionAddArray, actionAddString, actionAddNumber, actionAddBoolean, actionAddNull,
			actionRenameKey, actionDuplicateNode, actionDeleteNode, actionMoveUp, actionMoveDown,
			actionNormalizeArray, actionArrayToObjects, actionObjectsToArray,
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

		// Import / Export submenus are populated from the registered converters in Phase 10 (FILE-11); present but
		// empty-and-disabled here.

		QMenu* importMenu = fileMenu->addMenu ( tr ( "&Import" ) );
		QMenu* exportMenu = fileMenu->addMenu ( tr ( "&Export" ) );
		importMenu->setEnabled ( false );
		exportMenu->setEnabled ( false );

		fileMenu->addSeparator ();
		fileMenu->addAction ( actionPageSetup );
		fileMenu->addAction ( actionPrint );
		fileMenu->addSeparator ();

		QMenu* recentFilesMenu = fileMenu->addMenu ( tr ( "&Recent Files" ) );   // Populated in Phase 10 (FILE-05).
		recentFilesMenu->setEnabled ( false );

		fileMenu->addSeparator ();
		fileMenu->addAction ( actionSettings );
		fileMenu->addSeparator ();
		fileMenu->addAction ( actionExit );

		// Edit.

		QMenu* editMenu = menuBar ()->addMenu ( tr ( "&Edit" ) );
		editMenu->addAction ( actionFind );
		editMenu->addAction ( actionGoTo );
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

		QToolBar* toolBar = addToolBar ( tr ( "Main Toolbar" ) );
		toolBar->setObjectName ( QStringLiteral ( "mainToolBar" ) );   // Stable id for saveState/restoreState.
		toolBar->setMovable ( false );

		// Compact icon squares. With the label off screen every button has to carry its own
		// tooltip, so one is built below from the action's text plus its shortcut -- the toolbar teaches the keyboard.

		toolBar->setToolButtonStyle ( Qt::ToolButtonIconOnly );

		// A null entry is a group separator. The order is fixed; per-button visibility (SET-04)
		// and separator collapsing arrive with the Settings dialog.

		const QList<QAction*> toolBarActions =
		{
			actionNew, actionOpen, actionClose, actionSave, actionSaveAs,
			nullptr,
			actionUndo, actionRedo,
			nullptr,
			actionAddObject, actionAddArray, actionAddString, actionAddNumber, actionAddBoolean, actionAddNull
		};

		for ( QAction* toolBarAction : toolBarActions )
		{
			if ( toolBarAction == nullptr )
			{
				toolBar->addSeparator ();

				continue;
			}

			// Strip the mnemonic marker and the ellipsis so the tooltip reads as a plain command name.

			QString label = toolBarAction->text ();

			label.remove ( QLatin1Char ( '&' ) );
			label.remove ( QStringLiteral ( "..." ) );

			const QString shortcut = toolBarAction->shortcut ().toString ( QKeySequence::NativeText );

			toolBarAction->setToolTip ( shortcut.isEmpty () ? label : tr ( "%1 (%2)" ).arg ( label, shortcut ) );

			toolBar->addAction ( toolBarAction );
		}
	}

	void MainWindow::apply_action_icons ()
	{
		if ( icons == nullptr )
		{
			return;
		}

		// One table so the command -> glyph mapping is reviewable in a single place. Deliberately unadorned: Exit, the
		// three array transforms, the theme radio items, and the Help entries -- menu conventions leave those bare, and
		// an icon on a checkable radio item fights its check mark. The six type glyphs the library also carries are the
		// tree's (TREE-03), consumed in Phase 6.

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

		actions.cut       = actionCut;
		actions.copy      = actionCopy;
		actions.paste     = actionPaste;
		actions.duplicate = actionDuplicateNode;
		actions.remove    = actionDeleteNode;
		actions.rename    = actionRenameKey;
		actions.moveUp    = actionMoveUp;
		actions.moveDown  = actionMoveDown;

		return actions;
	}

	void MainWindow::create_workspace ()
	{
		// Two card-surface panes (STYLE-01/02) divided by a vertical splitter. The card QSS (STYLE-*)
		// is Phase 14, so a styled frame stands in here.

		auto make_card = [ this ] ( QWidget* content ) -> QFrame*
		{
			QFrame* card = new QFrame ( this );
			card->setFrameShape ( QFrame::StyledPanel );

			QVBoxLayout* cardLayout = new QVBoxLayout ( card );

			cardLayout->setContentsMargins ( 0, 0, 0, 0 );
			cardLayout->addWidget ( content );

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
			std::make_unique<FormViewProvider> ( document, undo, selection, settings, status, node_context_actions () )
		);

		editorPane->register_view ( std::make_unique<TextViewProvider> ( document, settings ) );

		editorPane->register_view ( std::make_unique<CodeViewProvider> ( document, undo, settings, status ) );

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

		QFrame* treeCard   = make_card ( treePane );
		QFrame* editorCard = make_card ( editorPane );

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

		// Selection -> status.

		connect ( selection, &SelectionService::selection_changed, this, [ this ] ( const JsonPointer&, SelectionOrigin ) { update_status_selection (); } );
		connect ( selection, &SelectionService::selection_cleared, this, &MainWindow::update_status_selection );

		// Status service -> the status bar's transient message area.

		connect ( status, &StatusService::message_posted,  this, [ this ] ( const QString& text, int timeout ) { statusBar ()->showMessage ( text, timeout ); } );
		connect ( status, &StatusService::message_cleared, this, [ this ] () { statusBar ()->clearMessage (); } );

		// -- Tree pane (Phase 6) --------------------------------------------------------------------------------------

		// Expand All / Collapse All are tree commands, so they go live with the tree (TREE-05). They are the first two
		// actions to leave the present-but-disabled set Phase 5 established.

		connect ( actionExpandAll,   &QAction::triggered, treePane, &TreeViewPane::expand_all );
		connect ( actionCollapseAll, &QAction::triggered, treePane, &TreeViewPane::collapse_all );

		connect ( document, &JsonDocument::reset, this, &MainWindow::update_tree_command_enablement );

		// The context menu shares the menu bar's QActions, so the two are enabled and disabled together (TREE-06). The
		// commands themselves are still Phase 9's -- what is wired here is the menu structure and the tree-scoped
		// shortcut context.

		treePane->set_context_actions ( node_context_actions () );

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

		const QString& filePath     = document->file_path ();
		const QString  documentName = filePath.isEmpty () ? tr ( "Untitled" ) : QFileInfo ( filePath ).fileName ();

		setWindowTitle ( tr ( "%1[*] - %2" ).arg ( documentName, application_name () ) );
		setWindowModified ( document->is_dirty () );
	}

	void MainWindow::update_status_document ()
	{
		if ( !document->has_root () )
		{
			statusDocumentLabel->setText ( tr ( "No document" ) );

			return;
		}

		const QString& filePath     = document->file_path ();
		const QString  documentName = filePath.isEmpty () ? tr ( "Untitled" ) : QFileInfo ( filePath ).fileName ();
		const QString  modifiedMark = document->is_dirty () ? QStringLiteral ( " *" ) : QString ();

		statusDocumentLabel->setText ( documentName + modifiedMark );
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

	void MainWindow::update_tree_command_enablement ()
	{
		// Nothing to expand or collapse without a document. Centralized enablement for the whole command surface lands
		// in Phase 9; this is the tree's own slice of it (disabled-not-hidden).

		const bool hasDocument = document->has_root ();

		actionExpandAll  ->setEnabled ( hasDocument );
		actionCollapseAll->setEnabled ( hasDocument );
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
		LoadResult result = DocumentIo::load_file ( path );

		if ( !result.ok )
		{
			// Malformed / unreadable input is reported with its position and never crashes (FILE-06).

			QMessageBox::critical
			(
				this,
				tr ( "Open Failed" ),
				tr ( "Could not open \"%1\".\n\n%2 (line %3, column %4)" )
					.arg ( QFileInfo ( path ).fileName (), result.error.message )
					.arg ( result.error.line )
					.arg ( result.error.column )
			);

			status->show_message ( tr ( "Failed to open %1" ).arg ( QFileInfo ( path ).fileName () ), 5000 );

			return false;
		}

		// Install the loaded tree, reset the undo baseline, and select the root (NAV-01). set_root
		// emits reset(), which refreshes the title and status panes.

		document->set_root ( std::move ( result.root ) );
		document->set_file_path ( path );
		document->set_dirty ( false );
		undo->clear ();

		selection->set_selection ( JsonPointer (), SelectionOrigin::Programmatic );

		// SET-03: duplicate keys are always kept; "Keep and warn" adds a non-blocking notice.

		if ( !result.duplicateKeys.empty () )
		{
			const QString onDuplicate = settings->value_string ( settings_keys::ON_DUPLICATE_KEYS, QStringLiteral ( "KeepSilently" ) );

			if ( onDuplicate == QLatin1String ( "KeepAndWarn" ) )
			{
				status->show_message ( tr ( "Loaded with %n duplicate key(s) preserved.", nullptr, static_cast<int> ( result.duplicateKeys.size () ) ), 5000 );
			}
		}

		// Persist the Recent Files list (FILE-05); the menu that surfaces it is wired in Phase 10.

		const QString    absolutePath = QFileInfo ( path ).absoluteFilePath ();
		QStringList      recentFiles  = settings->value_string_list ( settings_keys::RECENT_FILES );

		recentFiles.removeIf ( [ &absolutePath ] ( const QString& existing ) { return existing.compare ( absolutePath, Qt::CaseInsensitive ) == 0; } );
		recentFiles.prepend ( absolutePath );

		while ( recentFiles.size () > config::limits::RECENT_FILES )
		{
			recentFiles.removeLast ();
		}

		settings->set_string_list ( settings_keys::RECENT_FILES, recentFiles );

		status->show_message ( tr ( "Opened %1" ).arg ( QFileInfo ( path ).fileName () ), 3000 );

		return true;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void MainWindow::closeEvent ( QCloseEvent* event )
	{
		// EDITOR-09 names Exit among the departures that must not silently drop an uncommitted Code View edit. A valid
		// one commits here; an invalid one asks, and keeping it aborts the exit. This runs BEFORE the geometry is
		// persisted, since an aborted exit is not an exit.
		//
		// The FILE-08 dirty-DOCUMENT gate (an unsaved file, as distinct from an uncommitted view edit) is Phase 10 and
		// belongs after this one: there is no point asking whether to save a document whose latest edit has not
		// reached it yet.

		if ( ( editorPane != nullptr ) && !editorPane->confirm_leaving_active_view () )
		{
			event->ignore ();

			return;
		}

		persist_geometry ();

		event->accept ();
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPathView implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonPathView.hpp"

#include "AppConfig.hpp"
#include "services/IconLibrary.hpp"
#include "models/cell_presentation.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"
#include "services/settings_profiles.hpp"
#include "services/ClipboardService.hpp"
#include "views/Card.hpp"
#include "views/WorkspaceSplitter.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStringList>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vje
{
	//=================================================================================================================
	// File-Local Constants
	//=================================================================================================================

	namespace
	{
		// The column a row's pointer text is stored on, and the role it is stored under. The pointer is kept as TEXT
		// and re-parsed when the row is chosen, which is not a round trip taken for convenience: it is the same text
		// FIND-05 copies and FIND-04 accepts, so a row that could not be re-parsed here would be a row whose pointer
		// Go To could not take either.

		constexpr int POINTER_COLUMN = 0;
		constexpr int VALUE_COLUMN   = 1;

		constexpr int POINTER_ROLE = Qt::UserRole + 1;

		// The root's pointer is the empty string (RFC 6901), which would leave its row's first column blank. It is
		// named instead, in the words FindController's node_display_text already uses for the same node.

		QString pointer_display_text ( const JsonPointer& pointer )
		{
			return pointer.is_root () ? QStringLiteral ( "(root)" ) : pointer.to_string ();
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonPathView::JsonPathView
	(
		JsonDocument*     document,
		SelectionService* selection,
		SettingsStore*    settings,
		StatusService*    status,
		ClipboardService* clipboard,
		QWidget*          parent
	)
	:
		QWidget    ( parent ),
		document   ( document ),
		selection  ( selection ),
		settings   ( settings ),
		status     ( status ),
		clipboard  ( clipboard )
	{
		// THE VIEW'S OWN GROUND IS THE WINDOW BACKDROP, and that is what makes the two cards below correct rather than
		// merely present. A Card fills its corner wedges with QPalette::Window and derives its border as a style/tone
		// distance from the same role (Card.hpp, card_surface.hpp) -- both on the stated assumption that what lies
		// around a card is the backdrop. Dropped straight onto the editor card's Base surface, a nested card would cut
		// grey notches out of white at every rounded corner and draw a border toned for a colour that was not there.
		//
		// So rather than teach Card about a second backdrop, the view GIVES IT THE ONE IT EXPECTS: it paints Window
		// itself and insets the pair by the workspace's own margin, which is the same frame measurement STYLE-03 asks
		// to be consistent. The result is the workspace's composition nested one level down, using the workspace's
		// rules unchanged.

		setAutoFillBackground ( true );
		setBackgroundRole     ( QPalette::Window );

		// -- The results, above ------------------------------------------------------------------------------------

		resultsList = new QTreeWidget ( this );

		// No frame: the CARD draws the border now, and a sunken panel inside it would be a second edge a pixel in from
		// the first. This is what CodeEditor and TextView already do for the same reason.

		resultsList->setFrameShape ( QFrame::NoFrame );

		resultsList->setColumnCount     ( 2 );
		resultsList->setHeaderLabels    ( { tr ( "Pointer" ), tr ( "Value" ) } );
		resultsList->setRootIsDecorated ( false );
		resultsList->setUniformRowHeights ( true );
		resultsList->setSelectionMode   ( QAbstractItemView::SingleSelection );
		resultsList->setAllColumnsShowFocus ( true );

		resultsList->header ()->setSectionResizeMode ( POINTER_COLUMN, QHeaderView::Interactive );
		resultsList->header ()->setStretchLastSection ( true );

		resultsList->setAccessibleName ( tr ( "Query results" ) );

		// QUERY-09. CustomContextMenu rather than an override, so the position arrives with the request and the menu
		// can ask what sits under the cursor -- which is the whole of what decides its shape.

		resultsList->setContextMenuPolicy ( Qt::CustomContextMenu );

		connect
		(
			resultsList, &QTreeWidget::customContextMenuRequested,
			this, &JsonPathView::handle_results_context_menu
		);

		// -- The query, below --------------------------------------------------------------------------------------

		QWidget* const queryPane = new QWidget ( this );

		queryEdit = new QPlainTextEdit ( queryPane );

		queryEdit->setFrameShape        ( QFrame::NoFrame );
		queryEdit->setLineWrapMode      ( QPlainTextEdit::WidgetWidth );
		queryEdit->setPlaceholderText   ( QStringLiteral ( "$..name" ) );
		queryEdit->setAccessibleName    ( tr ( "JSONPath query" ) );
		queryEdit->installEventFilter   ( this );

		QVBoxLayout* const queryLayout = new QVBoxLayout ( queryPane );

		queryLayout->setContentsMargins ( 0, 0, 0, 0 );
		queryLayout->addWidget ( queryEdit );

		// -- The two cards (STYLE-01/02/05) ---------------------------------------------------------------------------
		//
		// One per pane, exactly as the workspace gives one to the tree and one to the editor. They bring the fill, the
		// one-pixel border and the SET-12 corner discipline with them, which is why the query view answers to Interface
		// style without knowing anything about how a corner is drawn.

		resultsCard = new Card ( this );
		queryCard   = new Card ( this );

		// SQUARE UNDER BOTH INTERFACE STYLES, and that is a rule rather than an exception to SET-12. The fillet exists
		// to soften a pane against the WINDOW BACKDROP (STYLE-02), and a card nested inside another card is not against
		// it -- the editor card around these two still fillets under Fluent, and rounding these as well would put an arc
		// inside an arc a few pixels away, which reads as a rendering fault rather than as a style.
		//
		// So there is no setting to read here and nothing to keep in step: the answer does not depend on the user's
		// choice, which is why this is two calls at construction rather than a reader, a signal and a push.

		resultsCard->set_top_corners_rounded ( false );
		queryCard->set_top_corners_rounded   ( false );

		resultsCard->add_content ( resultsList );
		queryCard->add_content   ( queryPane );

		// -- The split (STYLE-04) ---------------------------------------------------------------------------------

		// The SAME splitter class the workspace uses (STYLE-04), which is what makes its grip the same colour and the
		// same tones as the master-detail one. Fusion's own grip is two fixed translucent overlays rather than palette
		// colours, so a plain QSplitter here would have disappeared against the light theme exactly as the workspace's
		// did before WorkspaceSplitter existed -- and would have read as a different kind of control even where it was
		// visible. Nothing about the class is workspace-specific: it takes an orientation and paints its grip from
		// style/tone, and it already handles a horizontal handle.
		//
		// The THICKNESS is its own dial rather than the workspace's constant, because this splitter sits inside the
		// editor card between two content widgets where that one sits between two cards against the backdrop. It is
		// seeded from the workspace value, so the two match unless someone deliberately parts them.

		resultSplitter = new WorkspaceSplitter ( Qt::Vertical, this );

		resultSplitter->setHandleWidth ( config::editor::QUERY_SPLITTER_THICKNESS );

		resultSplitter->addWidget ( resultsCard );
		resultSplitter->addWidget ( queryCard );

		resultSplitter->setStretchFactor ( 0, config::editor::QUERY_RESULTS_STRETCH );
		resultSplitter->setStretchFactor ( 1, config::editor::QUERY_INPUT_STRETCH );

		resultSplitter->setChildrenCollapsible ( false );

		QVBoxLayout* const layout = new QVBoxLayout ( this );

		// The workspace's own margin, not a second number: the gap around the pair and the gap between them are one
		// frame (STYLE-03), and the splitter handle already occupies that same measurement between the cards.

		layout->setContentsMargins
		(
			config::workspace::CONTENT_MARGIN, config::workspace::CONTENT_MARGIN,
			config::workspace::CONTENT_MARGIN, config::workspace::CONTENT_MARGIN
		);

		layout->addWidget ( resultSplitter );

		// -- Wiring -------------------------------------------------------------------------------------------------
		//
		// A click and Enter are the same command (QUERY-06). Arrowing the list is deliberately NOT wired: moving
		// through a list is navigation, and only a gesture chooses (EDITOR-04).

		connect ( resultsList, &QTreeWidget::itemClicked,   this, &JsonPathView::handle_result_chosen );
		connect ( resultsList, &QTreeWidget::itemActivated, this, &JsonPathView::handle_result_chosen );

		if ( document != nullptr )
		{
			connect ( document, &JsonDocument::node_changed, this, &JsonPathView::handle_document_changed );
			connect ( document, &JsonDocument::reset,        this, &JsonPathView::handle_document_reset );
		}

		update_report ();
	}

	void JsonPathView::set_results_changed_callback ( std::function<void ()> callback )
	{
		resultsChanged = std::move ( callback );
	}

	//=================================================================================================================
	// IEditorView
	//=================================================================================================================

	QWidget* JsonPathView::widget ()
	{
		return this;
	}

	void JsonPathView::present ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		// A NO-OP, and it is the whole implementation. A query begins at the root, so what this view shows does not
		// depend on which node is selected -- and re-running on a selection change would make arrowing down the tree
		// re-walk the document once per key press for an answer that cannot have changed (QUERY-01, NFR-03).

		Q_UNUSED ( pointer )
		Q_UNUSED ( origin )
	}

	void JsonPathView::view_activated ()
	{
		if ( refresh_if_stale () )
		{
			update_report ();
		}
	}

	void JsonPathView::take_focus ()
	{
		queryEdit->setFocus ( Qt::TabFocusReason );
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void JsonPathView::run_query ()
	{
		const QString text = query_text ();

		const JsonPathQuery compiled = JsonPathQuery::compile ( text );

		// An empty query is not a mistake and not a failed search: it stands the view down entirely rather than
		// reporting "no results", which would claim the document had been looked at (QUERY-05).

		if ( text.trimmed ().isEmpty () )
		{
			lastRunText = QString ();

			resultPointers.clear ();

			hasRun = false;
			stale  = false;

			lastCompileError = JsonPathError ();

			show_results ();
			update_report ();

			return;
		}

		if ( !compiled.is_valid () )
		{
			// The previous results STAND. Emptying the list on a typo would read as an answer about the document
			// rather than as a complaint about the query (QUERY-05).

			lastCompileError = compiled.error ();

			mark_error_in_query_box ();
			update_report ();

			return;
		}

		lastCompileError = JsonPathError ();

		const JsonNode* const root = ( document != nullptr ) ? document->root () : nullptr;

		resultPointers = ( root != nullptr ) ? compiled.evaluate ( *root ) : std::vector<JsonPointer> ();

		lastRunText = text;
		hasRun      = true;
		stale       = false;

		show_results ();
		update_report ();
	}

	void JsonPathView::set_query_text ( const QString& text )
	{
		queryEdit->setPlainText ( text );
	}

	bool JsonPathView::copy_results ()
	{
		// Copying is one of QUERY-07's questions, so a stale set is re-run FIRST and what lands on the clipboard is the
		// answer for the document as it stands rather than the one the pane happened to be showing. No deferral is
		// needed here, unlike choosing a row: this is not called from inside a list item's own signal.

		if ( refresh_if_stale () )
		{
			update_report ();
		}

		if ( resultPointers.empty () || ( clipboard == nullptr ) )
		{
			return false;
		}

		// A list of POINTERS is a list of names, so it goes through set_plain_text -- see the naming rule at the head
		// of ClipboardService.hpp.

		clipboard->set_plain_text ( results_as_pointer_text () );

		if ( status != nullptr )
		{
			const int count = static_cast<int> ( resultPointers.size () );

			status->show_message
			(
				( count == 1 ) ? tr ( "Copied 1 JSON Pointer" ) : tr ( "Copied %1 JSON Pointers" ).arg ( count )
			);
		}

		return true;
	}

	bool JsonPathView::go_to_result ( const JsonPointer& pointer )
	{
		// Resolved against the document AS IT STANDS, which is the question a choice actually asks -- the row was taken
		// from a list that may predate an edit.

		const JsonNode* const node = ( document != nullptr ) ? document->resolve ( pointer ) : nullptr;

		if ( node == nullptr )
		{
			// Reported in place, and nothing changes. This is FIND-04's Unresolvable, reached from the other
			// direction: a pointer taken before an edit that removed what it named.

			if ( status != nullptr )
			{
				status->show_message ( tr ( "That node is no longer in the document." ) );
			}

			return false;
		}

		// GoTo, because that is exactly what this is: the same channel Edit > Go To publishes on, which already
		// reveals into a collapsed branch and already reports in the status bar (QUERY-06). The phase adds no
		// selection machinery of its own.

		if ( selection != nullptr )
		{
			selection->set_selection ( pointer, SelectionOrigin::GoTo );
		}

		// Choosing a result is one of QUERY-07's questions, so a stale query re-runs -- but DEFERRED, because the
		// re-run rebuilds the list and would destroy the very item whose signal this may be inside (architecture.md
		// section 7's standing rule about structural rebuilds inside a widget's own event handler). Deferring changes
		// no outcome: the pointer was resolved against the current document above, before any of it.

		if ( stale )
		{
			QMetaObject::invokeMethod
			(
				this,
				[ this ] () { if ( refresh_if_stale () ) { update_report (); } },
				Qt::QueuedConnection
			);
		}

		return true;
	}

	bool JsonPathView::copy_result_pointer ( const JsonPointer& pointer )
	{
		if ( clipboard == nullptr )
		{
			return false;
		}

		const QString text = pointer.to_string ();

		clipboard->set_plain_text ( text );

		if ( status != nullptr )
		{
			// FindController's words for the same outcome, deliberately rather than a second spelling of it: this is
			// FIND-05 reached from a result row instead of from the tree selection, and a user who copies a pointer
			// two ways should not be told two different things. The root's pointer is the empty string (RFC 6901), so
			// that one success genuinely leaves the clipboard empty and has to say so.

			status->show_message
			(
				pointer.is_root () ? tr ( "Copied the document root pointer (empty)" ) : tr ( "Copied %1" ).arg ( text )
			);
		}

		return true;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QString JsonPathView::query_text () const
	{
		return queryEdit->toPlainText ();
	}

	const std::vector<JsonPointer>& JsonPathView::results () const
	{
		return resultPointers;
	}

	bool JsonPathView::results_are_stale () const
	{
		return stale;
	}

	bool JsonPathView::has_results () const
	{
		// The LIST, not the run. A query that ran and matched nothing has nothing to copy, and so does one that was
		// never run -- QUERY-08's enablement cannot tell those apart and has no reason to.

		return !resultPointers.empty ();
	}

	QString JsonPathView::results_as_pointer_text () const
	{
		QStringList lines;

		lines.reserve ( static_cast<int> ( resultPointers.size () ) );

		for ( const JsonPointer& pointer : resultPointers )
		{
			// The POINTER, never the row's display text. The root's row is LABELLED "(root)" so that it is not blank
			// on screen, but what Go To accepts for it is the empty string (RFC 6901) -- so the root contributes an
			// empty line, and what is copied stays exactly what FIND-04 parses back.

			lines.append ( pointer.to_string () );
		}

		// One per line, which is the form every other tool takes a list of paths in and the form Go To reads one line
		// of. A separator that had to be stripped before use would make the command useless for its main purpose.

		return lines.join ( QChar ( '\n' ) );
	}

	const JsonPathError& JsonPathView::last_error () const
	{
		return lastCompileError;
	}

	QString JsonPathView::report () const
	{
		if ( !lastCompileError.message.isEmpty () )
		{
			return lastCompileError.message;
		}

		if ( !hasRun )
		{
			return QString ();
		}

		const int count = static_cast<int> ( resultPointers.size () );

		// "1 result" rather than "1 results", for the reason FindController spells its singular separately: a counter
		// that cannot get its own plural right reads as broken.

		QString text;

		if ( count == 0 )
		{
			text = tr ( "No results" );
		}
		else if ( count == 1 )
		{
			text = tr ( "1 result" );
		}
		else
		{
			text = tr ( "%1 results" ).arg ( count );
		}

		// The staleness is said out loud rather than left for the user to infer from a count that no longer matches
		// what they can see in the tree (QUERY-07).

		if ( stale )
		{
			text += QStringLiteral ( " " ) + tr ( "(document changed)" );
		}

		return text;
	}

	QTreeWidget* JsonPathView::results_view () const
	{
		return resultsList;
	}

	QPlainTextEdit* JsonPathView::query_box () const
	{
		return queryEdit;
	}

	QSplitter* JsonPathView::splitter () const
	{
		return resultSplitter;
	}

	Card* JsonPathView::results_card () const
	{
		return resultsCard;
	}

	Card* JsonPathView::query_card () const
	{
		return queryCard;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void JsonPathView::handle_document_changed ()
	{
		if ( !hasRun || stale )
		{
			return;
		}

		// Marked, not re-run. With the tab in the background nobody is looking, and an editing session that is not a
		// query session should cost a flag per edit rather than a walk of the document (QUERY-07, NFR-03).

		// MARKED SILENTLY. There is nowhere to say it that would not be a message from a tab the user may not be
		// looking at, arriving on top of the one the edit itself just posted -- so staleness is RESOLVED rather than
		// announced: the next question asked of this view re-runs the query and reports the answer that is true then
		// (QUERY-07).

		stale = true;
	}

	void JsonPathView::handle_document_reset ()
	{
		// Different in kind from an edit: the previous document's pointers name nothing, so the results are dropped
		// rather than marked stale. The QUERY is kept -- it is the user's, and it is very likely the first thing they
		// will want to run against whatever has just been loaded.

		resultPointers.clear ();

		hasRun = false;
		stale  = false;

		lastRunText = QString ();

		lastCompileError = JsonPathError ();

		show_results ();
		update_report ();
	}

	void JsonPathView::handle_result_chosen ( QTreeWidgetItem* item, int column )
	{
		Q_UNUSED ( column )

		if ( ( item == nullptr ) || choosingResult )
		{
			return;
		}

		// Held until the next turn of the event loop rather than to the end of this function, because a DOUBLE click
		// emits itemClicked twice and itemActivated once and all three are one choice. Releasing it here would let the
		// pair through and publish the same selection three times.

		choosingResult = true;

		QMetaObject::invokeMethod ( this, [ this ] () { choosingResult = false; }, Qt::QueuedConnection );

		bool ok = false;

		const JsonPointer pointer = JsonPointer::parse ( item->data ( POINTER_COLUMN, POINTER_ROLE ).toString (), &ok );

		if ( ok )
		{
			// The SAME command the context menu's Go to Node runs, deliberately: a click and a menu item that both
			// mean "take me to this node" must not be two implementations of it (QUERY-06 / QUERY-09).

			go_to_result ( pointer );
		}
	}

	void JsonPathView::handle_results_context_menu ( const QPoint& position )
	{
		// QUERY-09. The menu's shape follows what is UNDER THE CURSOR rather than what is selected, and a right click
		// deliberately does not move the selection: the menu names the row the user pointed at, so making that row
		// current first would be a second, invisible consequence of asking what the options are.

		QMenu menu ( this );

		populate_results_menu ( menu, resultsList->itemAt ( position ) );

		menu.exec ( resultsList->viewport ()->mapToGlobal ( position ) );
	}

	void JsonPathView::populate_results_menu ( QMenu& menu, QTreeWidgetItem* item )
	{
		if ( item != nullptr )
		{
			bool ok = false;

			const JsonPointer pointer =
				JsonPointer::parse ( item->data ( POINTER_COLUMN, POINTER_ROLE ).toString (), &ok );

			if ( ok )
			{
				// THE ROW COMMANDS COME FIRST, above a separator, because they act on the row the menu was opened on
				// while the command below acts on the whole list -- two different subjects, and putting the narrower
				// one first is the order every other context menu in this application uses.

				QAction* const goToAction    = menu.addAction ( tr ( "&Go to Node" ) );
				QAction* const copyOneAction = menu.addAction ( tr ( "&Copy JSON Pointer" ) );

				copyOneAction->setEnabled ( clipboard != nullptr );

				connect ( goToAction,    &QAction::triggered, this, [ this, pointer ] () { go_to_result ( pointer ); } );
				connect ( copyOneAction, &QAction::triggered, this, [ this, pointer ] () { copy_result_pointer ( pointer ); } );

				menu.addSeparator ();
			}
		}

		// The whole-set command is ALWAYS present and disabled rather than absent when there is nothing to copy --
		// the application's standing disabled-not-hidden rule (Phase 9). It is the same command as Edit > Copy
		// JSONPath Result and reports through the same path, so the two surfaces cannot come to disagree.

		QAction* const copyAllAction = menu.addAction ( tr ( "Copy JSONPath &Result" ) );

		copyAllAction->setEnabled ( has_results () && ( clipboard != nullptr ) );

		connect ( copyAllAction, &QAction::triggered, this, [ this ] () { copy_results (); } );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool JsonPathView::refresh_if_stale ()
	{
		if ( !stale || lastRunText.isEmpty () )
		{
			return false;
		}

		// The LAST RUN query, not the box's current contents: a stale refresh must not silently answer a question the
		// user has typed but not yet asked.

		const JsonPathQuery compiled = JsonPathQuery::compile ( lastRunText );

		const JsonNode* const root = ( document != nullptr ) ? document->root () : nullptr;

		resultPointers = ( compiled.is_valid () && ( root != nullptr ) )
		               ? compiled.evaluate ( *root )
		               : std::vector<JsonPointer> ();

		stale = false;

		show_results ();

		return true;
	}

	void JsonPathView::show_results ()
	{
		resultsList->clear ();

		for ( const JsonPointer& pointer : resultPointers )
		{
			QTreeWidgetItem* const item = new QTreeWidgetItem ( resultsList );

			item->setText ( POINTER_COLUMN, pointer_display_text ( pointer ) );
			item->setText ( VALUE_COLUMN,   value_preview ( pointer ) );

			item->setData ( POINTER_COLUMN, POINTER_ROLE, pointer.to_string () );
		}

		resultsList->resizeColumnToContents ( POINTER_COLUMN );

		// THE RESULT SET HAS CHANGED, and this is the one place that can say so: this function is called from exactly
		// the four points where the set can change -- a run, an emptied query, a stale refresh, and a document reset --
		// and from nowhere else. QUERY-08's enablement has no other trigger to ride on, because running a query changes
		// neither the selection, the document, the undo stack, the keyboard focus nor the clipboard.

		if ( resultsChanged )
		{
			resultsChanged ();
		}
	}

	void JsonPathView::update_report ()
	{
		// The application status bar, in its own left-hand message area, alongside every other outcome the application
		// reports (VAL-04, spec section 2.8). The view carries no report of its own: one place for a sentence about
		// what just happened, whichever component produced it.
		//
		// Called only from the paths a USER ACTION reaches -- running a query, choosing a result, the tab becoming
		// visible. A passive staleness mark deliberately does NOT come through here: the view may be in a background
		// tab, and posting from there would shout over the message the edit that caused it just posted.

		if ( status != nullptr )
		{
			const QString text = report ();

			// An empty report CLEARS rather than posting nothing, so an emptied query does not leave the previous
			// query's count standing where it would read as current (QUERY-05).

			if ( text.isEmpty () )
			{
				status->clear ();
			}
			else
			{
				status->show_message ( text );
			}
		}
	}

	void JsonPathView::mark_error_in_query_box ()
	{
		QTextCursor cursor = queryEdit->textCursor ();

		cursor.setPosition ( lastCompileError.position );

		if ( lastCompileError.length > 0 )
		{
			cursor.setPosition ( lastCompileError.position + lastCompileError.length, QTextCursor::KeepAnchor );
		}

		queryEdit->setTextCursor ( cursor );
	}

	QString JsonPathView::value_preview ( const JsonPointer& pointer ) const
	{
		const JsonNode* const node = ( document != nullptr ) ? document->resolve ( pointer ) : nullptr;

		if ( node == nullptr )
		{
			return QString ();
		}

		// The Form View's own presentation, not a second one: SET-03's notation and the shared {...} / [...]
		// placeholders arrive with it, so a value reads the same here as in the grid a click away (QUERY-06).

		QString text = cell_display_text ( node, string_display_mode ( settings ) );

		// A result row exists to be recognized and clicked; the node itself is one click away in the other views, so a
		// very long string is cut here rather than given the width of the pane.

		if ( text.size () > config::editor::QUERY_PREVIEW_MAXIMUM_CHARACTERS )
		{
			text = text.left ( config::editor::QUERY_PREVIEW_MAXIMUM_CHARACTERS ) + QStringLiteral ( "..." );
		}

		return text;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	bool JsonPathView::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( ( watched == queryEdit ) && ( event->type () == QEvent::KeyPress ) )
		{
			QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

			const bool isReturn = ( keyEvent->key () == Qt::Key_Return ) || ( keyEvent->key () == Qt::Key_Enter );

			// Enter RUNS and Shift+Enter breaks the line -- the other way round from a text editor, because a query box
			// exists to be run (QUERY-01). Any other modifier is left alone: Ctrl+Enter and friends belong to whoever
			// claims them, and quietly running the query on one of those would be a second, undocumented way in.

			if ( isReturn && ( keyEvent->modifiers () == Qt::NoModifier ) )
			{
				run_query ();

				return true;
			}
		}

		return QWidget::eventFilter ( watched, event );
	}

	//*****************************************************************************************************************
	// Class: JsonPathViewProvider
	//*****************************************************************************************************************

	const QString JsonPathViewProvider::VIEW_ID = QString::fromUtf8 ( config::editor::view_ids::JSONPATH );

	JsonPathViewProvider::JsonPathViewProvider
	(
		JsonDocument*     document,
		SelectionService* selection,
		SettingsStore*    settings,
		StatusService*    status,
		ClipboardService* clipboard
	)
	:
		document  ( document ),
		selection ( selection ),
		settings  ( settings ),
		status    ( status ),
		clipboard ( clipboard )
	{
	}

	void JsonPathViewProvider::set_results_changed_callback ( std::function<void ()> callback )
	{
		resultsChanged = std::move ( callback );
	}

	QString JsonPathViewProvider::view_id () const
	{
		return VIEW_ID;
	}

	QString JsonPathViewProvider::display_name () const
	{
		return QObject::tr ( "JSONPath" );
	}

	QString JsonPathViewProvider::icon_name () const
	{
		// The name the ARTWORK is drawn under (2026-08-16). This was "vje-view-query" and the drawings were
		// "vje-view-jsonpath", so the library found neither and the tab stayed bare however finished the art was --
		// a mismatch nothing could catch, because a name the set does not hold is a legitimate state (the tab shows
		// its label alone, IEditorView.hpp). The artwork's spelling won: it matches this view's id and the
		// vje-view-form / -text / -code pattern, where each is named for its view.
		//
		// Classic carries it at 16, 20, 24 and 25; the remaining rungs and the whole Fluent half are still to be
		// drawn, which the icon progress tracker records per file.

		return icon_names::VIEW_JSONPATH;
	}

	int JsonPathViewProvider::display_order () const
	{
		return DISPLAY_ORDER;
	}

	bool JsonPathViewProvider::can_present ( const JsonNode* node ) const
	{
		// The argument is IGNORED, and that is the requirement rather than an oversight (QUERY-01). A query begins at
		// the root, so what this view needs is a DOCUMENT -- and asking about the document rather than about the
		// selected node is what keeps the tab still while the tree selection moves.

		Q_UNUSED ( node )

		return ( document != nullptr ) && ( document->root () != nullptr );
	}

	IEditorView* JsonPathViewProvider::create_view ( QWidget* parent ) const
	{
		JsonPathView* const view = new JsonPathView ( document, selection, settings, status, clipboard, parent );

		// Every view this provider builds gets the window's results-changed hook, because EditorPane destroys and
		// rebuilds the view with its tab and there is no stable instance for MainWindow to connect to.

		view->set_results_changed_callback ( resultsChanged );

		return view;
	}
}

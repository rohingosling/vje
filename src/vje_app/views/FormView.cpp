//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FormView implementation -- the two grids, the presentation rule, drill-in, and the once-only column sizing. See the
//   header for the column-width rule and how the selection feedback loop is absorbed.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/FormView.hpp"

#include "AppConfig.hpp"
#include "models/JsonFormModel.hpp"
#include "models/JsonTableModel.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "style/FocusHighlight.hpp"
#include "views/FormGridController.hpp"
#include "views/JsonCellDelegate.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace vje
{
	const QString FormViewProvider::VIEW_ID = QStringLiteral ( "form" );

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FormView::FormView
	(
		JsonDocument*     document,
		UndoController*   undo,
		SelectionService* selection,
		SettingsStore*    settings,
		StatusService*    status,
		ClipboardService* clipboard,
		QWidget*          parent
	)
		: QWidget ( parent )
		, document  ( document )
		, selection ( selection )
		, settings  ( settings )
		, status    ( status )
		, clipboard ( clipboard )
	{
		formModel  = new JsonFormModel  ( document, undo, this );
		tableModel = new JsonTableModel ( document, undo, this );

		// -- The object form (EDITOR-02) -------------------------------------------------------------------------------

		formView = create_grid_view ();
		formView->setObjectName ( QStringLiteral ( "objectFormView" ) );
		formView->setModel ( formModel );

		// No headers and no grid: this is a FORM, and a labelled row should not read as a spreadsheet cell pair even
		// though it is built from one.

		formView->horizontalHeader ()->setVisible ( false );
		formView->verticalHeader   ()->setVisible ( false );
		formView->setShowGrid ( false );

		// NFR-05. The Form View has two faces and they are two different things to be told you are in -- a labelled
		// form where the key column names each row, and a spreadsheet where the header row names each column. One name
		// covering both would be less use than either.

		formView->setAccessibleName ( tr ( "Object form" ) );

		// EDITOR-02's layout rule, expressed directly: labels align to the widest sibling key, and the value column
		// takes whatever is left, so the editors grow and shrink with the window.
		//
		// These are PER-SECTION calls made before anything is presented, which is only safe because JsonFormModel
		// reports its two columns unconditionally -- see the note on its columnCount().

		formView->horizontalHeader ()->setSectionResizeMode ( JsonFormModel::KEY_COLUMN,   QHeaderView::ResizeToContents );
		formView->horizontalHeader ()->setSectionResizeMode ( JsonFormModel::VALUE_COLUMN, QHeaderView::Stretch );

		// -- The array table (EDITOR-03) -------------------------------------------------------------------------------

		tableView = create_grid_view ();
		tableView->setObjectName ( QStringLiteral ( "arrayTableView" ) );
		tableView->setModel ( tableModel );

		tableView->horizontalHeader ()->setVisible ( true );
		tableView->verticalHeader   ()->setVisible ( true );
		tableView->setShowGrid ( true );

		tableView->setAccessibleName ( tr ( "Array table" ) );

		// Interactive, and sized exactly once per presented node (see size_columns). Nothing re-measures a column on a
		// value change, which is what keeps the table from flapping while the user types (EDITOR-03).

		tableView->horizontalHeader ()->setSectionResizeMode ( QHeaderView::Interactive );

		// THE LAST COLUMN IS NOT STRETCHED, and the false is written out rather than left to the default because it was
		// true here and the two mechanisms actively contradicted each other.
		//
		// size_columns() clamps EVERY column to MAXIMUM_COLUMN_WIDTH, for the stated reason that one long value must not
		// claim the pane and push the other columns out of sight. stretchLastSection then handed the whole remaining
		// width to the last column, silently exempting it from that clamp -- so the rule held for every column except
		// the one most likely to hold a long value. On a SINGLE-column array (a scalar array, where the only column is
		// also the last one) it meant the column always spanned the entire pane, whatever the content measured.
		//
		// Leaving the space beyond the last column empty is the spreadsheet behaviour EDITOR-03 asks for, and the spec
		// says nothing about a stretched last column -- it was an implementation choice, not a requirement.

		tableView->horizontalHeader ()->setStretchLastSection ( false );

		// -- The empty state -------------------------------------------------------------------------------------------

		QLabel* const emptyLabel = new QLabel ( tr ( "No node selected" ), this );

		emptyLabel->setAlignment ( Qt::AlignCenter );
		emptyLabel->setEnabled ( false );

		emptyPage = emptyLabel;

		// -- Composition -----------------------------------------------------------------------------------------------

		pages = new QStackedWidget ( this );

		pages->addWidget ( emptyPage );
		pages->addWidget ( formView );
		pages->addWidget ( tableView );

		QVBoxLayout* const viewLayout = new QVBoxLayout ( this );

		viewLayout->setContentsMargins ( 0, 0, 0, 0 );
		viewLayout->addWidget ( pages );

		// -- Controllers -----------------------------------------------------------------------------------------------

		FormGridController::Policy formPolicy;

		formPolicy.writesSelectionBack = true;                         // Field focus selects the node (EDITOR-04).
		formPolicy.landingColumn       = JsonFormModel::VALUE_COLUMN;  // Land on the value; the key is a step away.
		formPolicy.contextMenuColumn   = JsonFormModel::KEY_COLUMN;    // The key right-click (EDITOR-02).

		FormGridController::Policy tablePolicy;

		tablePolicy.writesSelectionBack = false;   // In-place cell editing must not move the tree (EDITOR-04).

		// The cell clipboard (EDITOR-11) and provisional-row growth (EDITOR-12) are the ARRAY TABLE's alone, so only the
		// table controller receives the table support; the object form controller gets none (OQ-2).

		FormGridController::TableSupport tableSupport;

		tableSupport.model     = tableModel;
		tableSupport.clipboard = clipboard;
		tableSupport.settings  = settings;
		tableSupport.status    = status;

		formController  = new FormGridController ( formView,  formModel,  selection, formPolicy,  FormGridController::TableSupport (), this );
		tableController = new FormGridController ( tableView, tableModel, selection, tablePolicy, tableSupport, this );

		connect ( formController,  &FormGridController::drill_in_requested, this, &FormView::handle_drill_in );
		connect ( tableController, &FormGridController::drill_in_requested, this, &FormView::handle_drill_in );

		connect ( formController, &FormGridController::context_menu_requested, this, &FormView::handle_context_menu );

		// A refused commit keeps the editor open on the offending text (EDITOR-03), which on its own is indistinguishable
		// from an Enter key that did nothing. The reason travels with the refusal so the status bar can say it (VAL-04).

		for ( const FormGridController* const controller : { formController, tableController } )
		{
			connect
			(
				controller->delegate (), &JsonCellDelegate::commit_refused,
				this,                    &FormView::handle_commit_refused
			);
		}

		// SET-05 / EDIT-02. Read once here and re-pushed on change, rather than consulted from the model's flags(): the
		// value moves only when the Settings dialog is closed, and flags() is asked per cell per repaint (NFR-03).
		//
		// The ARRAY TABLE takes no equivalent -- its keys are column headers and it has no setHeaderData, so there is no
		// key-editing route through it to switch off.

		formModel->set_key_editing_allowed ( key_editing_allowed ( settings ) );

		apply_string_settings ();

		if ( settings != nullptr )
		{
			connect ( settings, &SettingsStore::changed, this, [ this ] ( const QString& key )
			{
				// this->settings, not the constructor parameter that shadows it here -- the lambda outlives the
				// constructor, so it must read the member.

				if ( key == settings_keys::FORM_ALLOW_KEY_EDITING )
				{
					formModel->set_key_editing_allowed ( key_editing_allowed ( this->settings ) );
				}

				// Both take effect at once, without a restart, like every other setting in the dialog. Filtered to the
				// two keys so an unrelated write (geometry, recent files) does not re-lay out a large table.

				if ( ( key == settings_keys::STRING_DISPLAY ) || ( key == settings_keys::FORM_WRAP_STRINGS ) )
				{
					apply_string_settings ();
				}
			} );
		}

		// A document load invalidates this view's memory of what it is showing -- see handle_document_reset for why that
		// memory cannot simply be left to the next present().

		if ( document != nullptr )
		{
			connect ( document, &JsonDocument::reset, this, &FormView::handle_document_reset );
		}

		// A value that grows needs its row re-measured (see remeasure_wrapped_rows). Connected to the MODEL's
		// dataChanged rather than to the undo stack, so a commit, an undo, a paste and an edit made in another view all
		// arrive by the same route.

		connect ( formModel, &QAbstractItemModel::dataChanged, this, &FormView::remeasure_wrapped_rows );
		connect ( formModel, &QAbstractItemModel::modelReset,  this, &FormView::remeasure_wrapped_rows );

		pages->setCurrentWidget ( emptyPage );
	}

	//=================================================================================================================
	// IEditorView
	//=================================================================================================================

	QWidget* FormView::widget ()
	{
		return this;
	}

	void FormView::present ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		const FormPresentation target = resolve_presentation ( document, pointer );

		if ( target.mode == FormPresentation::Mode::Nothing )
		{
			pages->setCurrentWidget ( emptyPage );

			formModel ->clear_presentation ();
			tableModel->clear_presentation ();

			currentMode      = FormPresentation::Mode::Nothing;
			currentContainer = JsonPointer ();
			currentHasFocus  = false;

			return;
		}

		// The grid REPORTING where its current field is, not asking to go anywhere (EDITOR-04). Answering it as a
		// navigation is what made arrowing down a form stop dead on the first container it reached: a container
		// presents ITSELF, so merely landing on one drilled the form into it, and an empty one has no rows to land on
		// next -- the fields vanished and the arrow keys went with them.
		//
		// Only the echo is refused, never a real navigation. Drill-in is a gesture and carries DrillIn; a document
		// load and the post-rebuild selection restore carry Programmatic and must still present, which is why this
		// asks for FormField specifically rather than the broader reveals_selection().

		if ( is_own_field_echo ( pointer, origin ) )
		{
			currentHasFocus = target.hasFocus;

			return;
		}

		// Presenting is otherwise IDEMPOTENT on the container, which keeps a re-present of what is already presented
		// from rebuilding a model -- so column widths, scroll position, and any open editor survive.

		const bool sameContainer = ( target.mode == currentMode ) && ( target.container == currentContainer );

		if ( !sameContainer )
		{
			if ( target.mode == FormPresentation::Mode::ObjectForm )
			{
				formModel->present ( target.container );

				pages->setCurrentWidget ( formView );
			}
			else
			{
				tableModel->present ( target.container );

				pages->setCurrentWidget ( tableView );

				// The one and only auto-size for this node.

				size_columns ( tableView );
			}

			currentMode      = target.mode;
			currentContainer = target.container;
		}

		FormGridController* const controller = ( target.mode == FormPresentation::Mode::ObjectForm )
		                                     ? formController
		                                     : tableController;

		if ( target.hasFocus )
		{
			controller->set_current_pointer ( target.focus );
		}
		else if ( !sameContainer )
		{
			// A container was selected in its own right, so there is no field to indicate -- start at the first one, so
			// the grid is immediately keyboard-navigable.

			controller->select_first_cell ();
		}

		// Remembered for the gesture handlers below: only a SCALAR selection indicates a field, and only an indicated
		// field is something the caret can be handed to.

		currentHasFocus = target.hasFocus;

		// ...and that is all. Presenting does not open an editor and does not take focus -- see IEditorView::present.
		// The Edit-on hand-over is driven by tree_node_clicked() / activate_editing(), because it answers a GESTURE.
	}

	void FormView::take_focus ()
	{
		// Whichever face is on screen, not the FormView itself -- focusing the container would leave the keyboard on a
		// widget with no current cell, so Tab would land in the pane and the arrow keys would do nothing (NAV-04).

		QWidget* const page = pages->currentWidget ();

		if ( ( page == nullptr ) || ( page == emptyPage ) )
		{
			return;
		}

		FormGridController* const controller = ( currentMode == FormPresentation::Mode::ObjectForm ) ? formController
		                                                                                             : tableController;

		// Arriving with nothing current is the one state the arrow keys cannot recover from on their own, so give the
		// grid a landing cell. This does NOT open an editor: focusing is not a gesture (EDITOR-04).

		if ( !static_cast<QTableView*> ( page )->currentIndex ().isValid () )
		{
			controller->select_first_cell ();
		}

		page->setFocus ( Qt::TabFocusReason );
	}

	void FormView::tree_node_clicked ()
	{
		// EDITOR-04's "Edit on: Single click", which is NOT the default: under it a click on a scalar in the tree both
		// presents its field and gives the user the caret. Under "Double click" -- the default -- the click only
		// presents, leaving the keyboard in the tree, and the caret waits for activate_editing().

		if ( hands_over_caret_on_selection () )
		{
			activate_editing ();
		}
	}

	void FormView::activate_editing ()
	{
		// The tree's unconditional activation gesture (Enter / double-click), and the "Edit on: Double click" half of
		// EDITOR-04.
		//
		// Only a scalar has a field to hand the caret to. Enter on a container is an EXPANSION gesture (NAV-02) that
		// happens to arrive here too, and opening an editor on the first member of the branch it just opened is not
		// what the user asked for -- so a container selection declines, and the keyboard stays in the tree.

		if ( !currentHasFocus )
		{
			return;
		}

		switch ( currentMode )
		{
			case FormPresentation::Mode::ObjectForm:
			{
				formController->activate_editing ();

				break;
			}

			case FormPresentation::Mode::ArrayTable:
			{
				tableController->activate_editing ();

				break;
			}

			case FormPresentation::Mode::Nothing:
			{
				break;
			}
		}
	}

	bool FormView::cell_cut ()
	{
		// The array table alone owns the cell clipboard (EDITOR-11). Declining on the object form (and the empty state)
		// lets Edit > Cut / Copy / Paste fall back to the node clipboard there (OQ-2).

		return ( currentMode == FormPresentation::Mode::ArrayTable ) && tableController->cut_cell ();
	}

	bool FormView::cell_copy ()
	{
		return ( currentMode == FormPresentation::Mode::ArrayTable ) && tableController->copy_cell ();
	}

	bool FormView::cell_paste ()
	{
		return ( currentMode == FormPresentation::Mode::ArrayTable ) && tableController->paste_cell ();
	}

	bool FormView::cell_clipboard_active () const
	{
		return ( currentMode == FormPresentation::Mode::ArrayTable ) && tableController->cell_clipboard_active ();
	}

	PrintContent FormView::print_content ( int availableColumns ) const
	{
		Q_UNUSED ( availableColumns )

		PrintContent content;

		if ( currentMode == FormPresentation::Mode::Nothing )
		{
			return content;
		}

		content.kind     = PrintContent::Kind::Table;
		content.viewName = tr ( "Form View" );
		content.subject  = currentContainer.to_string ();

		const QAbstractItemModel* const model =
			( currentMode == FormPresentation::Mode::ObjectForm )
				? static_cast<const QAbstractItemModel*> ( formModel )
				: static_cast<const QAbstractItemModel*> ( tableModel );

		const int columnCount = model->columnCount ();

		// The object form's columns are unlabelled on screen, so they stay unlabelled on paper; the array table's
		// headers are its column keys (EDITOR-03), read from the model rather than re-derived from the node.

		if ( currentMode == FormPresentation::Mode::ArrayTable )
		{
			for ( int column = 0; column < columnCount; ++column )
			{
				content.headers.append ( model->headerData ( column, Qt::Horizontal, Qt::DisplayRole ).toString () );
			}
		}

		const int rowCount = model->rowCount ();

		for ( int row = 0; row < rowCount; ++row )
		{
			// EDITOR-12's provisional row is a view-only affordance for GROWING an array -- it is not in the document
			// and there is nothing in it, so printing it would put an empty row on the page for every array printed.

			if ( ( tableModel != nullptr ) && ( currentMode == FormPresentation::Mode::ArrayTable )
			     && tableModel->is_provisional_row ( row ) )
			{
				continue;
			}

			QStringList cells;

			for ( int column = 0; column < columnCount; ++column )
			{
				cells.append ( model->data ( model->index ( row, column ), Qt::DisplayRole ).toString () );
			}

			content.rows.append ( cells );
		}

		return content;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	const JsonPointer& FormView::presented_pointer () const
	{
		return currentContainer;
	}

	FormPresentation::Mode FormView::presentation_mode () const
	{
		return currentMode;
	}

	QTableView* FormView::object_form_view () const
	{
		return formView;
	}

	QTableView* FormView::array_table_view () const
	{
		return tableView;
	}

	JsonFormModel* FormView::form_model () const
	{
		return formModel;
	}

	JsonTableModel* FormView::table_model () const
	{
		return tableModel;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void FormView::set_context_actions ( const NodeContextActions& actions )
	{
		contextActions = actions;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void FormView::handle_drill_in ( const JsonPointer& pointer )
	{
		// EDITOR-05. Drill-in is a navigation, so it goes through the selection service like any other -- the tree
		// follows and expands to reveal (DrillIn reveals, EDITOR-04), and this view is re-presented from the outside
		// rather than reaching into its own models.

		if ( selection != nullptr )
		{
			selection->set_selection ( pointer, SelectionOrigin::DrillIn );
		}
	}

	void FormView::handle_document_reset ()
	{
		// A load (or New, or Close) replaces the document wholesale, and both grid models clear themselves on this same
		// signal. What is left standing is THIS view's record of which node it is presenting -- and that record is a claim
		// about a document that no longer exists.
		//
		// It cannot be left for the next present() to sort out, because a new document's root carries the SAME pointer as
		// the old one's: the idempotence check below (which exists so a re-present does not rebuild a model and lose
		// column widths, scroll position, and any open editor) would judge the incoming root to be what is already on
		// screen and skip the rebuild -- leaving the pane blank until the user selected some other node and came back.
		// That was the reported defect. Dropping the record is what makes the present that follows a load a real one.
		//
		// Note this is deliberately NOT the Code View commit path: a commit swaps the root without emitting reset(),
		// precisely so it is not conflated with a load (NAV-03).

		currentMode      = FormPresentation::Mode::Nothing;
		currentContainer = JsonPointer ();
		currentHasFocus  = false;

		pages->setCurrentWidget ( emptyPage );
	}

	void FormView::handle_commit_refused ( const QString& reason )
	{
		if ( status != nullptr )
		{
			status->show_message ( reason, config::form::REFUSAL_MESSAGE_TIMEOUT );
		}
	}

	void FormView::handle_context_menu ( const JsonPointer& pointer, const QPoint& globalPosition )
	{
		Q_UNUSED ( pointer );

		// The same commands the tree offers, acting on the key's node (EDITOR-02). The controller has already made that
		// row current, which wrote the selection back with the no-reveal FormField origin -- so the commands, which
		// read the selection, target the right node without this menu needing to say so.

		QMenu menu ( this );

		populate_node_context_menu ( &menu, contextActions );

		menu.exec ( globalPosition );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void FormView::remeasure_wrapped_rows ()
	{
		// A commit can turn a one-line value into a paragraph, and QAbstractItemView::dataChanged repaints WITHOUT
		// asking the vertical header to recompute its ResizeToContents sections -- so the row kept its old height and
		// the tail of the value the user had just typed was clipped, with no scroll bar inside the cell to reach it
		// (2026-07-28 review). Only the object form wraps, and only while the setting is on.

		if ( ( formView != nullptr ) && formView->wordWrap () )
		{
			formView->resizeRowsToContents ();
		}
	}

	void FormView::apply_string_settings ()
	{
		// SET-03 lives in the MODELS, because they produce the display text, the edit text and the commit -- three
		// answers that have to agree and would not if each read the setting for itself.

		const StringDisplay mode = string_display_mode ( settings );

		formModel->set_string_display ( mode );
		tableModel->set_string_display ( mode );

		// SET-05 lives in the VIEWS and the delegate: it is about how much room a value gets, not what it says.
		//
		// THE OBJECT FORM ONLY (revised 2026-07-27). It applied to both faces first, on the reasoning that one setting
		// should mean one thing -- but the two faces are not one thing here. A form field is a labelled paragraph and
		// wraps the way a form should; a TABLE cell is a spreadsheet cell, and rows of varying height break the one
		// property a spreadsheet is read for, which is that a row is a row. So the array table keeps one-line elided
		// cells and their tooltips whatever this setting says, and the asymmetry is the requirement rather than an
		// oversight (SET-05).

		const bool wrap = wrap_strings_in_form_view ( settings );

		if ( formController != nullptr )
		{
			formController->delegate ()->set_wrap_strings ( wrap );
		}

		// The table's own delegate is never told about wrapping, so a table cell keeps its single-line editor and the
		// grid keeps its uniform rows -- there is nothing to switch back when the setting goes off.

		{
			QTableView* const gridView = formView;

			gridView->setWordWrap ( wrap );

			// Elision and wrapping are alternatives, not companions: leave ElideRight on and a wrapped cell still ends
			// in an ellipsis on its FIRST line, which is the one shape that reads as broken rather than as either.

			gridView->setTextElideMode ( wrap ? Qt::ElideNone : Qt::ElideRight );

			QHeaderView* const rows = gridView->verticalHeader ();

			// EACH ROW IS SIZED TO ITS OWN CONTENT while wrapping -- a one-line value gets one line however long its
			// neighbour is. A single uniform height was tried first, to keep the row geometry arithmetic, and it is
			// plainly wrong to look at: a form of ten short fields beside one paragraph became ten paragraphs of white
			// space.
			//
			// The cost is measurement: ResizeToContents makes the header measure rows rather than multiply one height.
			// It is affordable HERE and would not have been in the array table, which is the other half of why this
			// setting is the object form's alone -- an object has tens of members, an array can have hundreds of
			// thousands (architecture.md section 10). Wrapping is also OFF by default, so only a user who asked for it
			// pays even that.
			//
			// There is NO sampling bound, and there was never a working one: QHeaderView::setResizeContentsPrecision on
			// a VERTICAL header caps the number of COLUMNS sampled per row, and this grid has two -- so the call read as
			// protection while doing nothing (2026-07-28 review). What actually bounds the cost is the coalescing in
			// create_grid_view above, plus the two facts already stated: this is the object form, and wrapping is off by
			// default.
			//
			// Nothing bounds a row's HEIGHT: a value is shown whole however narrow the column gets (SET-05), which is
			// what per-pixel vertical scrolling above makes safe.

			const int oneLine = gridView->fontMetrics ().height () + config::form::ROW_VERTICAL_PADDING;

			if ( wrap )
			{
				// A floor at the ordinary row height. Content measurement lands a pixel or two under it for a short
				// value, so without this every one-line row would twitch as wrapping is switched on -- a change the
				// user did not ask for on the rows they were not asking about.

				rows->setMinimumSectionSize ( oneLine );
				rows->setSectionResizeMode ( QHeaderView::ResizeToContents );
			}
			else
			{
				rows->setSectionResizeMode ( QHeaderView::Fixed );
				rows->setDefaultSectionSize ( oneLine );
			}
		}
	}

	QTableView* FormView::create_grid_view ()
	{
		QTableView* const gridView = new QTableView ( this );

		gridView->setAlternatingRowColors ( false );
		gridView->setWordWrap ( false );
		gridView->setCornerButtonEnabled ( false );
		gridView->setHorizontalScrollMode ( QAbstractItemView::ScrollPerPixel );

		// Vertically per pixel too, which is what lets a wrapped row be as tall as its value needs (SET-05). Scrolling
		// per ITEM through a grid whose rows vary in height moves the viewport in jumps the size of whichever row is
		// next -- and it is the reason a row-height cap looked necessary before this line existed.

		gridView->setVerticalScrollMode ( QAbstractItemView::ScrollPerPixel );

		// A column that narrows needs MORE lines for the same value, and the row heights are measured against the
		// column widths -- so a width change is a re-measure. Only while wrapping: with one-line rows there is nothing
		// a width could change.
		//
		// This is what the object form's stretching value column makes routine rather than rare: every resize of the
		// pane resizes that column, so without it a value re-wraps inside a row that kept its old height and the last
		// line is simply not drawn.

		connect
		(
			gridView->horizontalHeader (), &QHeaderView::sectionResized,
			gridView, [ this, gridView ] ( int section, int, int newWidth )
			{
				if ( !gridView->wordWrap () )
				{
					return;
				}

				// COALESCED on an actual change to the column the values wrap in. The value column stretches, so a
				// splitter or window drag emits sectionResized on every mouse move -- and re-measuring there ran a full
				// text layout per row per pixel, which stalled the drag and could oscillate at the scroll-bar threshold
				// (the re-measure adds the bar, which resizes the viewport, which re-stretches the column). Filtering to
				// the value column and to a width that really changed is the same trick TextView::resizeEvent uses to
				// keep a resize from re-rendering on every pixel.

				if ( ( section != JsonFormModel::VALUE_COLUMN ) || ( newWidth == measuredValueColumnWidth ) )
				{
					return;
				}

				measuredValueColumnWidth = newWidth;

				gridView->resizeRowsToContents ();
			}
		);

		// Uniform, fixed row heights. As with the tree (TREE-08), this is what lets the view compute its visible range
		// arithmetically instead of measuring every row -- the virtualization half of handling a large array.

		const int rowHeight = gridView->fontMetrics ().height () + config::form::ROW_VERTICAL_PADDING;

		gridView->verticalHeader ()->setSectionResizeMode ( QHeaderView::Fixed );
		gridView->verticalHeader ()->setDefaultSectionSize ( rowHeight );

		// The selection bar answers to keyboard focus, so the accent is on whichever grid the user is actually driving
		// and the tree's bar mutes while they work here (STYLE-12). Installed per grid rather than on the FormView --
		// only one of the two is ever on screen, and each must answer for itself.

		FocusHighlight::install ( gridView );

		return gridView;
	}

	void FormView::size_columns ( QTableView* gridView )
	{
		const QAbstractItemModel* const model = gridView->model ();

		// Let Qt measure the content first (sizeHintForColumn itself is protected), then bound the result. This is the
		// ONLY place a column width is computed -- see the header for why nothing re-measures afterwards.

		gridView->resizeColumnsToContents ();

		const QHeaderView* const header = gridView->horizontalHeader ();

		for ( int column = 0; column < model->columnCount (); ++column )
		{
			// The wider of the content and the header -- a short column of values under a long key name would otherwise
			// elide the name it is identified by.

			const int naturalWidth = std::max ( gridView->columnWidth ( column ), header->sectionSizeHint ( column ) )
			                       + config::form::COLUMN_PADDING;

			gridView->setColumnWidth
			(
				column,
				std::clamp ( naturalWidth, config::form::MINIMUM_COLUMN_WIDTH, config::form::MAXIMUM_COLUMN_WIDTH )
			);
		}
	}

	bool FormView::is_own_field_echo ( const JsonPointer& pointer, SelectionOrigin origin ) const
	{
		// FormField is written by exactly one thing -- FormGridController, when the current cell of a grid THIS view
		// owns has moved (EDITOR-04). So an incoming selection wearing it is this view hearing its own voice.
		//
		// The parent test is what keeps that claim honest rather than assumed: the only pointers this view can have
		// just published are children of the container it is presenting, so anything else carrying the origin is
		// somebody else's navigation and is presented normally.

		if ( ( origin != SelectionOrigin::FormField ) || ( currentMode == FormPresentation::Mode::Nothing ) )
		{
			return false;
		}

		return !pointer.is_root () && ( pointer.parent () == currentContainer );
	}

	bool FormView::hands_over_caret_on_selection () const
	{
		// SET-05, whose default is DOUBLE CLICK. Shared with the Code View's SET-07 equivalent so the two views cannot
		// end up disagreeing about what a tree click means (settings_profiles.hpp).

		return hands_over_caret_on_click ( settings, settings_keys::FORM_EDIT_ON );
	}

	//=================================================================================================================
	// FormViewProvider
	//=================================================================================================================

	FormViewProvider::FormViewProvider
	(
		JsonDocument*             document,
		UndoController*           undo,
		SelectionService*         selection,
		SettingsStore*            settings,
		StatusService*            status,
		ClipboardService*         clipboard,
		const NodeContextActions& contextActions
	)
		: document       ( document )
		, undo           ( undo )
		, selection      ( selection )
		, settings       ( settings )
		, status         ( status )
		, clipboard      ( clipboard )
		, contextActions ( contextActions )
	{
	}

	QString FormViewProvider::view_id () const
	{
		return VIEW_ID;
	}

	QString FormViewProvider::display_name () const
	{
		return QObject::tr ( "Form" );
	}

	QString FormViewProvider::icon_name () const
	{
		return QStringLiteral ( "vje-view-form" );
	}

	int FormViewProvider::display_order () const
	{
		return DISPLAY_ORDER;
	}

	bool FormViewProvider::can_present ( const JsonNode* node ) const
	{
		// The Form View presents every node kind -- containers directly, scalars through their parent (EDITOR-02). Only
		// an empty document leaves it nothing to show.

		return node != nullptr;
	}

	IEditorView* FormViewProvider::create_view ( QWidget* parent ) const
	{
		FormView* const view = new FormView ( document, undo, selection, settings, status, clipboard, parent );

		view->set_context_actions ( contextActions );

		return view;
	}
}

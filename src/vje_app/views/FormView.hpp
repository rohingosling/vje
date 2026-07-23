//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FormView -- the default editor view and the core of the product (EDITOR-02..05). One widget with two faces, chosen
//   by what is selected:
//
//     - An OBJECT (or a scalar document root) renders as a labeled edit form -- one row per member, the label column
//       sized to the widest sibling key and the value column filling the rest (EDITOR-02).
//     - An ARRAY renders as a table -- one row per element, one column per member key for an array of objects, a single
//       value column otherwise (EDITOR-03).
//     - A SCALAR renders its PARENT's form or table with the corresponding field or cell current. That resolution is
//       the view's, not the selection service's, and is stated in resolve_presentation() below.
//
//   Both faces are QTableViews over the same delegate and the same controller, which is what makes EDITOR-02's "form /
//   table parity" structural rather than a promise (see FormGridController).
//
//   THE COLUMN-WIDTH RULE. Columns are sized ONCE, when a node is presented, and are Interactive thereafter. Nothing
//   re-measures them on a value change -- which is what EDITOR-03 means by "a cell commit refreshes values in the
//   existing table rather than rebuilding it", and what stops a long value from making the whole table flap while the
//   user types. The auto-size is bounded by config::form::MAXIMUM_COLUMN_WIDTH so one long first-row value
//   cannot claim the pane.
//
//   THE SELECTION LOOP, AND HOW IT IS BROKEN. The form writes its current field back with SelectionOrigin::FormField
//   (EDITOR-04), which comes straight back in as a present(). That echo is REFUSED outright (is_own_field_echo) -- it
//   reports where the user's field cursor is, and this view is the thing that moved it, so there is nothing to do.
//
//   Idempotence on the container is not enough on its own, and the difference is the whole bug it was hiding: a scalar
//   resolves to its PARENT, which is the container already presented, so scalar echoes cancel out and the loop looks
//   closed. A container resolves to ITSELF -- so arrowing onto one drilled the form into it, and onto an empty one
//   left a pane with no rows and dead arrow keys. Landing on a container cell is navigation; drilling into it is a
//   gesture (EDITOR-05). The table does not write back at all, so it has no loop to break.
//
//   WHAT IS NOT HERE. Cell cut / copy / paste (EDITOR-11) and provisional rows (EDITOR-12) are Phase 9.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/IEditorView.hpp"
#include "views/NodeContextActions.hpp"
#include "views/node_presentation.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QWidget>

class QPoint;
class QStackedWidget;
class QTableView;

namespace vje
{
	class FormGridController;
	class JsonDocument;
	class JsonFormModel;
	class JsonTableModel;
	class SelectionService;
	class SettingsStore;
	class StatusService;
	class UndoController;

	//-----------------------------------------------------------------------------------------------------------------
	// What a selection resolves to. The rule -- particularly "a scalar presents its parent" -- is the one piece of
	// EDITOR-02 that is pure logic, and it now lives in node_presentation.hpp because the Text View is DEFINED as a
	// plain-text rendering of this same presentation (EDITOR-06). The alias is kept so the Form View still talks about
	// its own presentation in its own vocabulary.
	//-----------------------------------------------------------------------------------------------------------------

	using FormPresentation = NodePresentation;

	//*****************************************************************************************************************
	// Class: FormView
	//*****************************************************************************************************************

	class FormView : public QWidget, public IEditorView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The settings store may be null, in which case the EDITOR-04 "Edit on" behaviour falls back to its documented
		// default of Double click. The status service may be null, which simply silences the refusal messages.

		FormView
		(
			JsonDocument*     document,
			UndoController*   undo,
			SelectionService* selection,
			SettingsStore*    settings,
			StatusService*    status,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// IEditorView
		//=============================================================================================================

	public:

		QWidget* widget () override;

		void present ( const JsonPointer& pointer, SelectionOrigin origin ) override;

		void take_focus        () override;
		void tree_node_clicked () override;
		void activate_editing  () override;

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The pointer this view is currently presenting a form or table FOR -- the container, not the selection. A root
		// pointer when nothing is presented (check presentation_mode()).

		const JsonPointer& presented_pointer () const;

		FormPresentation::Mode presentation_mode () const;

		QTableView* object_form_view () const;
		QTableView* array_table_view () const;

		JsonFormModel*  form_model  () const;
		JsonTableModel* table_model () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// The shared node commands offered when a key label is right-clicked (EDITOR-02).

		void set_context_actions ( const NodeContextActions& actions );

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_drill_in       ( const JsonPointer& pointer );
		void handle_context_menu   ( const JsonPointer& pointer, const QPoint& globalPosition );

		// A commit the editor refused (a malformed number, or a key that already exists). The editor stays open on the
		// offending text; this puts the reason in the status bar so the refusal is not silent (VAL-04).

		void handle_commit_refused ( const QString& reason );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		QTableView* create_grid_view ();                        // The shared QTableView configuration.

		void size_columns ( QTableView* gridView );             // The once-only auto-size (see the header).

		// Is this incoming selection the view hearing its own field-focus write-back, rather than being asked to
		// navigate? See the implementation, and the selection-loop note in the file header.

		bool is_own_field_echo ( const JsonPointer& pointer, SelectionOrigin origin ) const;

		// Whether a single click in the tree is this view's activation gesture -- SET-05's "Edit on", which now defaults
		// to Double click (EDITOR-04).

		bool hands_over_caret_on_selection () const;

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		SettingsStore*    settings;
		StatusService*    status;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned) and models.
		//=============================================================================================================

	private:

		QStackedWidget* pages = nullptr;

		QTableView* formView  = nullptr;
		QTableView* tableView = nullptr;
		QWidget*    emptyPage = nullptr;

		JsonFormModel*  formModel  = nullptr;
		JsonTableModel* tableModel = nullptr;

		FormGridController* formController  = nullptr;
		FormGridController* tableController = nullptr;

		NodeContextActions contextActions;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		FormPresentation::Mode currentMode = FormPresentation::Mode::Nothing;
		JsonPointer            currentContainer;

		// Does what is presented INDICATE a field -- i.e. was the selection a scalar? The gesture handlers turn on this:
		// the caret is handed to an indicated field and to nothing else.

		bool currentHasFocus = false;
	};

	//*****************************************************************************************************************
	// Class: FormViewProvider
	//*****************************************************************************************************************

	class FormViewProvider : public IEditorViewProvider
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The node context actions are taken at registration rather than set on the view afterwards, because views are
		// created LAZILY -- a setter on the pane would miss any view built after it was called.

		FormViewProvider
		(
			JsonDocument*             document,
			UndoController*           undo,
			SelectionService*         selection,
			SettingsStore*            settings,
			StatusService*            status,
			const NodeContextActions& contextActions = NodeContextActions ()
		);

		//=============================================================================================================
		// IEditorViewProvider
		//=============================================================================================================

	public:

		QString view_id       () const override;
		QString display_name  () const override;
		QString icon_name     () const override;
		int     display_order () const override;

		bool can_present ( const JsonNode* node ) const override;

		IEditorView* create_view ( QWidget* parent ) const override;

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static const QString VIEW_ID;

		static constexpr int DISPLAY_ORDER = 0;   // The default tab, and first in the strip (EDITOR-01).

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		UndoController*   undo;
		SelectionService* selection;
		SettingsStore*    settings;
		StatusService*    status;

		NodeContextActions contextActions;
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonFormModel -- the QAbstractTableModel behind the Form View's OBJECT form (EDITOR-02): one row per member, a
//   read-only key label in column 0 and the member's value in column 1.
//
//   WHY A TABLE MODEL FOR A FORM. EDITOR-02 spells out "form / table parity": a field and a table cell must use the
//   same minimal presentation and the same interaction model -- plain text at rest, a current-item highlight, an editor
//   only on an explicit activation gesture. Building the form on the same model/view/delegate machinery as the array
//   table is what makes that parity structural rather than a pair of implementations kept in step by hand. The label
//   column being ResizeToContents and the value column Stretch is also exactly EDITOR-02's layout rule -- labels align
//   to the widest sibling key, values grow and shrink with the window.
//
//   THE LONE SCALAR ROOT. A scalar SELECTION presents its parent's form (the Form View resolves that, not this model),
//   so the only scalar this model ever projects directly is a scalar document ROOT, which has no parent and no key.
//   It renders as a single row with an empty label (EDITOR-02).
//
//   DUPLICATE KEYS ARE PRESENTED BUT NOT EDITABLE. A loaded file may hold duplicate sibling keys and VJE preserves them
//   (FILE-04, VAL-02), but a JSON Pointer names the FIRST member with a key -- so committing an edit to the second of
//   two "name" members would silently write the first. Rather than corrupt the document, this model shows every
//   duplicate and marks all of them read-only; Document > Rename Key is the repair. Editing them properly needs
//   positional addressing, which the pointer-based edit layer does not have.
//
//   Rows are diffed by JsonNode ADDRESS against a shadow vector, for the same reasons as JsonTableModel.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "models/IGridProjection.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/json_escapes.hpp>

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>

#include <vector>

namespace vje
{
	class UndoController;

	//*****************************************************************************************************************
	// Class: JsonFormModel
	//*****************************************************************************************************************

	class JsonFormModel : public QAbstractTableModel, public IGridProjection
	{
		Q_OBJECT

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static constexpr int KEY_COLUMN    = 0;   // The member key; a read-only label (EDITOR-02).
		static constexpr int VALUE_COLUMN  = 1;   // The member's value; the editable field.
		static constexpr int COLUMN_COUNT  = 2;

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// A null undo controller yields a read-only projection, which is the form the projection tests use.

		JsonFormModel ( JsonDocument* document, UndoController* undo = nullptr, QObject* parent = nullptr );

		//=============================================================================================================
		// QAbstractTableModel
		//=============================================================================================================

	public:

		int rowCount    ( const QModelIndex& parent = QModelIndex () ) const override;
		int columnCount ( const QModelIndex& parent = QModelIndex () ) const override;

		QVariant      data  ( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
		Qt::ItemFlags flags ( const QModelIndex& index ) const override;

		bool setData ( const QModelIndex& index, const QVariant& value, int role = Qt::EditRole ) override;

		//=============================================================================================================
		// Presentation
		//=============================================================================================================

	public:

		// Project the object the pointer names, or a scalar document root as a single keyless row. Anything else --
		// including an array, which is the TABLE's job -- presents nothing.

		void present ( const JsonPointer& objectPointer );

		void clear_presentation ();

		bool               is_presenting     () const;
		const JsonPointer& presented_pointer () const;

		//=============================================================================================================
		// Policy
		//=============================================================================================================

	public:

		// SET-05 / EDIT-02. Pushed IN by FormView rather than read from a SettingsStore here, for two reasons: flags()
		// is asked once per cell per repaint, so a store read per call is an NFR-03 cost for a value that changes only
		// when the user closes the Settings dialog; and the model stays free of settings, so a test states the policy
		// directly instead of standing up a store.
		//
		// Re-flags the key column when the value actually changes, so an open form reflects the new setting at once
		// rather than at the next presentation.

		void set_key_editing_allowed ( bool allowed );

		bool is_key_editing_allowed () const;

		//=============================================================================================================
		// Row Addressing
		//=============================================================================================================

	public:

		JsonNode*   value_node      ( int row ) const;   // The member's value; nullptr for an out-of-range row.
		QString     key_for_row     ( int row ) const;   // Empty for the lone scalar root.
		JsonPointer pointer_for_row ( int row ) const;   // The member's pointer; the root pointer for a scalar root.

		int row_for_pointer ( const JsonPointer& pointer ) const;   // -1 when the pointer is not one of these rows.

		//=============================================================================================================
		// IGridProjection
		//
		// BOTH columns answer with the ROW's value, label column included: a gesture on a label acts on the field it
		// names (EDITOR-02's right-click on a key), and activating a label opens the field's editor rather than trying
		// to edit the label itself -- which is what grid_edit_cell redirects.
		//=============================================================================================================

	public:

		JsonNode*    grid_node      ( int row, int column ) const override;
		JsonPointer  grid_pointer   ( int row, int column ) const override;
		GridPosition grid_cell      ( const JsonPointer& pointer ) const override;
		GridPosition grid_edit_cell ( int row, int column ) const override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_document_reset ();
		void handle_node_changed   ( const JsonPointer& pointer, DocumentChange change );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void rebuild      ();
		void resync       ();
		void capture_rows ();

		bool is_scalar_root_form () const;                       // The keyless single-row case.

		// The one editability rule, asked by both flags() and the commit path -- a scalar value whose key is not
		// duplicated (see the header). Shared deliberately: a guard applied only in flags() decorates the UI while the
		// write path walks past it.

		bool is_editable_row ( int row ) const;

		// Whether the row's KEY can be renamed (EDIT-02). A separate question from is_editable_row: that one asks
		// whether the value opens an editor, and an object's key renames just as well as a string's.

		bool is_renameable_row ( int row ) const;

		// The object's other keys, which a rename must not collide with (VAL-02).

		QStringList rival_keys_for_row ( int row ) const;

		bool covers ( const JsonPointer& pointer ) const;        // Is pointer the object itself or inside it?

		int  row_for_descendant ( const JsonPointer& pointer ) const;   // The row a deep change belongs to; -1 if none.

		bool commit_row ( int row, const QVariant& value );
		bool rename_row ( int row, const QString& newKey );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// How string values are shown and typed (SET-03). Pushed in by the view, which owns the settings; the model
		// holds it because the model is what produces the display text, the edit text, and the commit -- three answers
		// that have to agree, and would not if each read the setting for itself.

		void set_string_display ( StringDisplay mode );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonDocument*   document;                                // Non-owning.
		UndoController* undo;                                    // Non-owning; may be null (read-only projection).

		// The SET-03 notation. Decoded is the identity, which is what a model constructed without a view gets -- a test
		// then sees the raw characters unless it says otherwise.

		StringDisplay stringDisplay = StringDisplay::Decoded;


		JsonPointer objectPointer;
		JsonNode*   objectNode = nullptr;                        // Non-owning; null when nothing is presented.

		// Shadow rows, compared by address only and never dereferenced during a diff.

		std::vector<JsonNode*> rowNodes;
		QStringList            rowKeys;                          // Parallel to rowNodes; one empty entry for a scalar root.

		bool keyEditingAllowed = true;                           // SET-05; the default is Yes, and a model with no view.
	};
}

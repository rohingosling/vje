//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonCellDelegate -- the per-kind editor and the minimal at-rest presentation, shared by the Form View's object form
//   (EDITOR-02) and its array table (EDITOR-03). One delegate serves both because the two specs describe the same
//   control: plain text at rest with no editor chrome, an editor only on an explicit activation gesture, and the
//   editor's kind chosen by the value's kind.
//
//   The delegate never down-casts a model. It asks for cell_roles::CONTENT_KIND and cell_roles::VALUE_KIND, which both
//   Form View models answer, and is otherwise indifferent to whether it is painting a field or a cell.
//
//   PRESENTATION. Containers paint in the palette's link colour -- the drill-in colour EDITOR-02 asks for, taken from
//   the palette so it tracks the theme (no hard-coded colours). Null and missing cells paint
//   dimmed AND italic; the italic is deliberate, because the dim alone is lost the moment the cell is selected and the
//   highlight brush takes over, and the placeholder cue matters most on the cell the user is standing on.
//
//   THE KEYBOARD MODEL WHILE EDITING, and it is now the SAME model in both grids (2026-07-28):
//
//     - VERTICAL commits and moves. Up / Down leave for the field or cell above or below, because a single-line editor
//       has nowhere for them to go. Two editors keep the unmodified pair for themselves: a boolean's combo uses it to
//       change its value, and a WRAPPED multi-line string uses it to walk its own lines.
//     - CTRL+VERTICAL always commits and moves, from EVERY editor kind including those two. It is the one escape with
//       no exceptions, and it is free -- a stock QPlainTextEdit does nothing with Ctrl+Up / Ctrl+Down.
//     - HORIZONTAL belongs to the text. Left / Right clear the selection the editor opened with and walk the caret
//       through the value, in the object form AND in the array table. So does SHIFT+vertical: it selects a line at a
//       time, which is what it does in every other text box.
//
//   The table used to read Left / Right as spreadsheet navigation while the form read them as caret keys, and the
//   delegate carried a constructor flag to say which. That divergence is gone: it made a typo in the middle of a table
//   cell unreachable, since the only routes back were to retype the value or to Esc and start again.
//
//   Enter commits and moves down (Shift+Enter up), and Tab / Shift+Tab commit and move one cell in reading order, in
//   both. Unlike Qt's own Tab handling the target is NOT re-opened for editing -- the spec's Tab moves the highlight,
//   it does not chain edits.
//
//   A commit is refused outright when the editor's input is not acceptable (VAL-03): the keystroke is swallowed and the
//   caret stays in the errored cell, which is EDITOR-03's stated behaviour.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/grid_navigation.hpp"

#include <QStyledItemDelegate>

namespace vje
{
	//*****************************************************************************************************************
	// Class: JsonCellDelegate
	//*****************************************************************************************************************

	class JsonCellDelegate : public QStyledItemDelegate
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonCellDelegate ( QObject* parent = nullptr );

		//=============================================================================================================
		// QStyledItemDelegate
		//=============================================================================================================

	public:

		QWidget* createEditor
		(
			QWidget*                    parent,
			const QStyleOptionViewItem& option,
			const QModelIndex&          index
		) const override;

		// The wrapped height of a cell, bounded. QStyledItemDelegate already measures wrapped text -- QTableView sets
		// QStyleOptionViewItem::WrapText from its own wordWrap() -- so all this adds is the CAP, which is what stops one
		// pathological value becoming a row hundreds of lines tall.

		QSize sizeHint ( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

		void setEditorData ( QWidget* editor, const QModelIndex& index ) const override;
		void setModelData  ( QWidget* editor, QAbstractItemModel* model, const QModelIndex& index ) const override;

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Does a STRING open in a multi-line editor (SET-05)? Pushed in by the view, which owns the setting, and
		// changeable at runtime because the setting is. Scoped to strings on purpose: every other kind keeps the
		// QLineEdit that carries its validator, and with it VAL-02 / VAL-03's refusal behaviour.

		void set_wrap_strings ( bool wrap );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The editor was committed and closed by a movement key; the grid controller moves the current cell. Emitted
		// AFTER the commit, so the model already holds the new value when the current cell moves.

		void editing_moved ( GridMove move );

		// A commit was attempted and refused because the editor's text is not acceptable -- a malformed number
		// (VAL-03) or a key that already exists in the object (VAL-02). The editor stays open on the offending text;
		// this is the cue to say why.

		void commit_refused ( const QString& reason );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	protected:

		void initStyleOption ( QStyleOptionViewItem* option, const QModelIndex& index ) const override;

	private:

		// The editor itself, before it is named. Split out of createEditor so the accessible name (NFR-05) is applied
		// once to whatever this returns, rather than in each of the five branches -- where a sixth editor kind would
		// arrive anonymous and nothing would say so.

		QWidget* create_editor_widget ( QWidget* parent, const QModelIndex& index ) const;

		// Commit and close, then announce the movement. Returns false -- and changes nothing -- when the editor's
		// current input is not acceptable, which is what keeps the caret in an errored number cell (VAL-03).

		bool commit_and_move ( QWidget* editor, GridMove move );

		// The movement a key implies while an editor is open, or false when the key is the editor's own to handle.
		//
		// The editor matters: a boolean combo owns Up / Down to change its value, so those keys are left to it. Enter
		// and Tab still commit and move, which is how a keyboard user leaves a boolean field.

		bool movement_for_key
		(
			const QWidget*        editor,
			int                   key,
			Qt::KeyboardModifiers modifiers,
			GridMove&             outMove
		) const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		bool wrapStrings = false;
	};
}

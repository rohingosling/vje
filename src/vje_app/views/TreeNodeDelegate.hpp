//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TreeNodeDelegate -- the tree's item delegate, which paints TREE-10's unsaved-change dot after a row's label.
//
//   WHY A DELEGATE, AND NOT AN ICON. A tree row already carries its type glyph in Qt::DecorationRole (TREE-03), and a
//   row has one decoration. A composite icon -- the glyph with a dot stamped on it -- would be a second copy of every
//   glyph in both families, re-made on every theme change, for a mark that is not a glyph at all. So the row is painted
//   as Qt paints it, and the dot is drawn on top, from JsonTreeModel::CHANGE_MARK_ROLE.
//
//   WHERE IT GOES. Right after the label, config::tree::CHANGE_MARK_GAP clear of it -- not at the column's right edge.
//   The column is sized to its widest label (ResizeToContents), which in a narrow pane is routinely wider than the
//   viewport; a column of dots at its far edge would then be scrolled out of sight, which is exactly where a mark must
//   not be. The size hint reserves the room on EVERY row, marked or not, so a dot appearing never changes the column's
//   width and nothing on screen shifts when the user types.
//
//   ITS COLOUR. SET-14a's, pushed in by the window for the theme in effect (set_mark_colour); the palette's accent until
//   one is, which is the form a bare delegate in a test has. On a SELECTED row it is the row's own text colour instead,
//   whatever SET-14a says: the focused selection bar is the accent, and a dot of any colour near it would vanish into
//   it. The text colour is what the palette already guarantees reads on that bar -- white on the focused accent, the
//   ordinary text colour on the muted unfocused bar (STYLE-12) -- so the dot inherits a contrast someone has already
//   chosen.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QStyledItemDelegate>

namespace vje
{
	//*****************************************************************************************************************
	// Class: TreeNodeDelegate
	//*****************************************************************************************************************

	class TreeNodeDelegate : public QStyledItemDelegate
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit TreeNodeDelegate ( QObject* parent = nullptr );

		//=============================================================================================================
		// QStyledItemDelegate
		//=============================================================================================================

	public:

		void  paint    ( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
		QSize sizeHint ( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// Where the dot for this row goes, in the view's coordinates -- whether or not the row is marked. The one
		// statement of the geometry, so a test asking where to look and the paint deciding where to draw cannot
		// disagree.

		QRectF change_mark_rect ( const QStyleOptionViewItem& option, const QModelIndex& index ) const;

		QColor mark_colour () const;                           // Invalid until one is set: the accent is painted.

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// SET-14a, as it shows in the theme in effect -- the window converts it, so the delegate paints what it is given
		// and knows nothing of themes. The view is repainted by whoever sets it.

		void set_mark_colour ( const QColor& colour );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QColor markColour;
	};
}

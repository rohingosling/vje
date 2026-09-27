//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TreeNodeDelegate implementation. See TreeNodeDelegate.hpp for where the dot goes and why.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/TreeNodeDelegate.hpp"

#include "AppConfig.hpp"
#include "models/JsonTreeModel.hpp"

#include <QApplication>
#include <QPainter>
#include <QStyle>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TreeNodeDelegate::TreeNodeDelegate ( QObject* parent )
		: QStyledItemDelegate ( parent )
	{
	}

	//=================================================================================================================
	// QStyledItemDelegate
	//=================================================================================================================

	void TreeNodeDelegate::paint ( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
	{
		QStyledItemDelegate::paint ( painter, option, index );

		if ( !index.data ( JsonTreeModel::CHANGE_MARK_ROLE ).toBool () )
		{
			return;
		}

		// The colour group the style paints the row in (QCommonStyle's own rule), so the dot is read from the same
		// group as the bar and the text beside it.

		QPalette::ColorGroup group = QPalette::Disabled;

		if ( option.state & QStyle::State_Enabled )
		{
			group = ( option.state & QStyle::State_Active ) ? QPalette::Active : QPalette::Inactive;
		}

		const bool selected = ( option.state & QStyle::State_Selected );

		QColor colour = option.palette.color ( group, QPalette::Accent );

		if ( selected )
		{
			colour = option.palette.color ( group, QPalette::HighlightedText );
		}
		else if ( markColour.isValid () )
		{
			colour = markColour;
		}

		painter->save ();

		painter->setRenderHint ( QPainter::Antialiasing, true );
		painter->setPen        ( Qt::NoPen );
		painter->setBrush      ( colour );
		painter->drawEllipse   ( change_mark_rect ( option, index ) );

		painter->restore ();
	}

	QSize TreeNodeDelegate::sizeHint ( const QStyleOptionViewItem& option, const QModelIndex& index ) const
	{
		// Room for the dot on every row, marked or not -- see the header. The column is sized from this, so a mark
		// appearing never widens it.

		const QSize base = QStyledItemDelegate::sizeHint ( option, index );

		return QSize ( base.width () + config::tree::CHANGE_MARK_GAP + config::tree::CHANGE_MARK_DIAMETER, base.height () );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QColor TreeNodeDelegate::mark_colour () const
	{
		return markColour;
	}

	void TreeNodeDelegate::set_mark_colour ( const QColor& colour )
	{
		markColour = colour;
	}

	QRectF TreeNodeDelegate::change_mark_rect ( const QStyleOptionViewItem& option, const QModelIndex& index ) const
	{
		QStyleOptionViewItem itemOption = option;

		initStyleOption ( &itemOption, index );

		const QWidget* const widget = itemOption.widget;
		const QStyle*  const style  = ( widget != nullptr ) ? widget->style () : QApplication::style ();

		// The label's rectangle as the style lays it out, and the label's own width inside it: the style draws the text
		// from the rectangle's left edge, one focus-frame margin in (QCommonStyle's viewItemDrawText), and the delegate's
		// extra width leaves the rectangle wider than the text.

		const QRect textRect   = style->subElementRect ( QStyle::SE_ItemViewItemText, &itemOption, widget );
		const int   textMargin = style->pixelMetric ( QStyle::PM_FocusFrameHMargin, nullptr, widget ) + 1;
		const int   textWidth  = QFontMetrics ( itemOption.font ).horizontalAdvance ( itemOption.text );

		// Where the text ends -- or, if the column has somehow left it too little room, where the room ends, so the dot
		// is never drawn over the label.

		const int textRight = textRect.left () + textMargin + std::min ( textWidth, std::max ( 0, textRect.width () - ( 2 * textMargin ) ) );

		const qreal diameter = config::tree::CHANGE_MARK_DIAMETER;
		const qreal left     = textRight + config::tree::CHANGE_MARK_GAP;
		const qreal top      = textRect.top () + ( ( textRect.height () - diameter ) / 2.0 );

		return QRectF ( left, top, diameter, diameter );
	}
}

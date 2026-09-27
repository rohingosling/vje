//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GroupBox implementation. See the header for why the frame, the padding and the title are this class's rather than
//   Qt's.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/GroupBox.hpp"

#include "style/dialog_surface.hpp"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRect>
#include <QStyle>

#include <algorithm>

namespace vje
{
	namespace
	{
		// The title as Qt's mnemonic parser must see it for the text to come out as written: every ampersand doubled,
		// which QKeySequence::mnemonic skips and the text renderer shows as one.

		QString escape_mnemonics ( const QString& plainText )
		{
			QString escaped = plainText;

			escaped.replace ( QLatin1Char ( '&' ), QStringLiteral ( "&&" ) );

			return escaped;
		}

		// The title's font: the box's own, in bold, so the title reads as the heading of the rows beneath it rather than
		// as one more label among them. Measured and drawn from the same font, so the break in the outline fits it.

		QFont title_font ( const QFont& boxFont )
		{
			QFont font = boxFont;

			font.setBold ( true );

			return font;
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	GroupBox::GroupBox ( const QString& title, QWidget* parent )
		: QGroupBox  ( parent )
		, plainTitle ( title )
	{
		// Escaped for Qt, which keeps the title for the accessible name (read back with the doubling undone) and for its
		// own minimum size. The title is DRAWN from plainTitle, below.

		setTitle ( escape_mnemonics ( plainTitle ) );

		// setTitle has just re-derived the padding from the style; this box's own replaces it.

		apply_padding ();
	}

	//=================================================================================================================
	// Accessors
	//=================================================================================================================

	QString GroupBox::plain_title () const
	{
		return plainTitle;
	}

	bool GroupBox::corners_rounded () const
	{
		return cornersRounded;
	}

	int GroupBox::frame_top () const
	{
		if ( plainTitle.isEmpty () )
		{
			return 0;
		}

		// The line runs through the middle of the title's line box, which is what sets the title INTO the edge rather
		// than on top of it or beneath it.

		return QFontMetrics ( title_font ( font () ) ).height () / 2;
	}

	QRect GroupBox::title_rect () const
	{
		if ( plainTitle.isEmpty () )
		{
			return QRect ();
		}

		const QFontMetrics metrics ( title_font ( font () ) );

		// Inset to the content's own left edge (config::group_box::TITLE_INSET), so the title lines up with the labels
		// beneath it -- measured, like the content, from inside the outline.

		const int left = config::group_box::FRAME_THICKNESS + config::group_box::TITLE_INSET;

		return QRect ( left, 0, metrics.horizontalAdvance ( plainTitle ), metrics.height () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void GroupBox::set_corners_rounded ( bool rounded )
	{
		if ( rounded == cornersRounded )
		{
			return;
		}

		cornersRounded = rounded;

		update ();
	}

	//=================================================================================================================
	// Event Handlers
	//=================================================================================================================

	void GroupBox::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event );

		QPainter painter ( this );

		painter.setRenderHint ( QPainter::Antialiasing, true );

		// The outline, from the title's centre line down, as a FILLED RING -- the area between the box's edge and the same
		// shape one thickness inside it -- rather than a stroked path. A one-pixel pen is drawn by Qt's thin-line stroker,
		// which has no joins: every square corner came out at 75 % coverage, a soft grey dot, whatever join the pen asked
		// for (measured, Qt 6.10.1; tst_group_box). A fill covers each pixel by its area, so a square corner is a whole
		// pixel of ink and a rounded one is antialiased like the arc it is. A zero radius is a square box, which is
		// Classic; the inner radius is one thickness less, so the ring stays one thickness wide round the curve.

		const qreal thickness = config::group_box::FRAME_THICKNESS;
		const qreal radius    = cornersRounded ? config::group_box::CORNER_RADIUS : 0.0;

		const QRectF outer = QRectF ( 0.0, frame_top (), width (), height () - frame_top () );
		const QRectF inner = outer.adjusted ( thickness, thickness, -thickness, -thickness );

		const qreal innerRadius = std::max ( radius - thickness, 0.0 );

		// Two nested outlines under QPainterPath's default odd-even fill: the ring between them.

		QPainterPath outline;

		outline.addRoundedRect ( outer, radius,      radius );
		outline.addRoundedRect ( inner, innerRadius, innerRadius );

		// The break for the title: the outline stops TITLE_GAP short of the text on either side. Cut by clipping rather
		// than by walking the outline in two pieces, so the corners stay one construction whatever the radius.

		const QRect title = title_rect ();

		if ( !title.isEmpty () )
		{
			const int gap = config::group_box::TITLE_GAP;

			QPainterPath visible;

			visible.addRect ( QRectF ( rect () ) );

			QPainterPath titleBreak;

			titleBreak.addRect ( QRectF ( title.left () - gap, 0.0, title.width () + ( gap * 2 ), frame_top () + thickness + 1.0 ) );

			painter.setClipPath ( visible.subtracted ( titleBreak ) );
		}

		painter.fillPath ( outline, dialog_group_frame ( palette () ) );

		painter.setClipping ( false );

		// The title, as given and in the ordinary text colour -- drawItemText takes the disabled colour from the palette
		// when the box is disabled, so a greyed box greys its name with it. No mnemonic flag, so an ampersand is drawn as
		// itself (see the header).

		if ( !title.isEmpty () )
		{
			painter.setFont ( title_font ( font () ) );

			style ()->drawItemText
			(
				&painter,
				title,
				Qt::AlignLeft | Qt::AlignVCenter,
				palette (),
				isEnabled (),
				plainTitle,
				QPalette::WindowText
			);
		}
	}

	void GroupBox::changeEvent ( QEvent* event )
	{
		// QGroupBox re-derives its padding from the style on these two, inside its own changeEvent -- so it runs FIRST,
		// and this box's padding goes back on over the top of it (see the header).

		QGroupBox::changeEvent ( event );

		if ( ( event->type () == QEvent::StyleChange ) || ( event->type () == QEvent::FontChange ) )
		{
			apply_padding ();
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void GroupBox::apply_padding ()
	{
		// Outline to content, the same on every side, measured from INSIDE the outline -- and at the top, from the line
		// the title sits on rather than from the top of the title (config::group_box).

		const int side = config::group_box::FRAME_THICKNESS + config::group_box::PADDING;

		setContentsMargins ( side, frame_top () + side, side, side );
	}
}

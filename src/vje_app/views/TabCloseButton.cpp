//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TabCloseButton implementation. See the header for why the widget is replaced rather than the style overridden, and
//   for why the toolkit's own Normal / Disabled rule is reproduced rather than dropped.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/TabCloseButton.hpp"

#include <QEnterEvent>
#include <QPainter>
#include <QPixmap>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TabCloseButton::TabCloseButton
	(
		const QSize& buttonSize,
		const QSize& glyphSize,
		QWidget*     parent
	)
		: QAbstractButton ( parent )
		, buttonSize      ( buttonSize )
		, glyphSize       ( glyphSize )
	{
		setFocusPolicy ( Qt::NoFocus );

		// The hover state is read from the widget in paintEvent, so the tracking has to be on whether or not a button
		// is pressed -- without it a bare move over the button raises no event and the glyph never brightens.

		setMouseTracking ( true );

		setCursor ( Qt::ArrowCursor );

		resize ( buttonSize );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void TabCloseButton::set_icon ( const QIcon& icon )
	{
		glyph = icon;

		update ();
	}

	void TabCloseButton::set_on_current_tab ( bool onCurrentTab )
	{
		if ( onCurrentTab == this->onCurrentTab )
		{
			return;
		}

		this->onCurrentTab = onCurrentTab;

		update ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QSize TabCloseButton::sizeHint () const
	{
		return buttonSize;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void TabCloseButton::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event )

		if ( glyph.isNull () )
		{
			return;
		}

		// QCommonStyle's own rule for PE_IndicatorTabClose, reproduced: Active while the pointer is on it or it is
		// held down, Normal on the tab the user is actually on, and Disabled everywhere else -- which is what makes the
		// x quiet on the tabs the user is not using. What Disabled LOOKS like belongs to the icon family, not here.

		const QIcon::Mode mode = ( isDown () || underMouse () ) ? QIcon::Active
		                       : onCurrentTab                   ? QIcon::Normal
		                                                        : QIcon::Disabled;

		const QPixmap pixmap = glyph.pixmap ( glyphSize, devicePixelRatioF (), mode, QIcon::Off );

		if ( pixmap.isNull () )
		{
			return;
		}

		// Centred in the button rather than filling it, for the reason the sizes are separate: the glyph is drawn at an
		// authored size and the button is the click target around it.

		const QSize   drawnSize = pixmap.deviceIndependentSize ().toSize ();
		const QPointF origin    ( ( width () - drawnSize.width () ) / 2.0, ( height () - drawnSize.height () ) / 2.0 );

		QPainter painter ( this );

		painter.drawPixmap ( origin, pixmap );
	}

	void TabCloseButton::enterEvent ( QEnterEvent* event )
	{
		QAbstractButton::enterEvent ( event );

		update ();
	}

	void TabCloseButton::leaveEvent ( QEvent* event )
	{
		QAbstractButton::leaveEvent ( event );

		update ();
	}
}

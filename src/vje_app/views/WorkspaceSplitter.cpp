//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   WorkspaceSplitter implementation. See the header for why the grip is painted here rather than left to the style.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/WorkspaceSplitter.hpp"

#include "AppConfig.hpp"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	WorkspaceSplitterHandle::WorkspaceSplitterHandle ( Qt::Orientation orientation, QSplitter* parent )
		: QSplitterHandle ( orientation, parent )
	{
		// STYLE-04's hover affordance needs hover events, which a plain widget does not receive.

		setAttribute ( Qt::WA_Hover );
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void WorkspaceSplitterHandle::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event );

		QPainter painter ( this );

		const QColor ground = palette ().color ( QPalette::Window );

		// The hover tint is the same derivation as the grip, just a much smaller step -- so it reads as the same
		// surface catching the light rather than as a second colour arriving from somewhere else.

		painter.fillRect
		(
			rect (),
			underMouse () ? contrasting_tone ( ground, config::workspace::GRIP_HOVER_CONTRAST ) : ground
		);

		const int dotSize = config::workspace::GRIP_DOT_SIZE;
		const int dotGap  = config::workspace::GRIP_DOT_GAP;
		const int dots    = config::workspace::GRIP_DOT_COUNT;

		const bool isVerticalHandle = ( orientation () == Qt::Horizontal );

		const int gripLength = ( dots * dotSize ) + ( ( dots - 1 ) * dotGap );

		// Centred on both axes by construction. With an 8 px handle and a 2 px grip that puts the dots at offsets 3-4,
		// where the style's own grip sits at 4-5 -- a pixel right of centre, and visibly so.

		const int acrossOrigin = ( ( isVerticalHandle ? width  () : height () ) - dotSize    ) / 2;
		const int alongOrigin  = ( ( isVerticalHandle ? height () : width  () ) - gripLength ) / 2;

		const QColor mainTone  = contrasting_tone ( ground, config::workspace::GRIP_MAIN_CONTRAST );
		const QColor bevelTone = contrasting_tone ( ground, config::workspace::GRIP_BEVEL_CONTRAST );

		for ( int index = 0; index < dots; ++index )
		{
			const int along = alongOrigin + ( index * ( dotSize + dotGap ) );

			const QRect dot = isVerticalHandle ? QRect ( acrossOrigin, along, dotSize, dotSize )
			                                   : QRect ( along, acrossOrigin, dotSize, dotSize );

			painter.fillRect ( dot, mainTone );

			// One corner in the softer tone, which is what gives each dot its slight bevel. Bottom-left reads as lit
			// from the top left, matching the direction the rest of the toolkit's shading assumes.

			painter.fillRect ( QRect ( dot.left (), dot.bottom (), 1, 1 ), bevelTone );
		}
	}

	//=================================================================================================================
	// WorkspaceSplitter
	//=================================================================================================================

	WorkspaceSplitter::WorkspaceSplitter ( Qt::Orientation orientation, QWidget* parent )
		: QSplitter ( orientation, parent )
	{
	}

	QSplitterHandle* WorkspaceSplitter::createHandle ()
	{
		return new WorkspaceSplitterHandle ( orientation (), this );
	}
}

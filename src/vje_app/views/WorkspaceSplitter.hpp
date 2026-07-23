//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   WorkspaceSplitter -- the master-detail splitter, with a grip that is legible in BOTH themes (STYLE-04).
//
//   WHY IT PAINTS ITS OWN GRIP. Fusion draws the grip with two FIXED translucent overlays -- a white one and a black
//   one -- rather than palette colours, so how visible it is depends entirely on what happens to be underneath. On the
//   dark theme's #2D2D30 the white overlay lands +73 lightness and reads clearly; on the light theme's #F3F3F3 it
//   lands +4 and disappears. Measured, not inferred.
//
//   THE PRINCIPLE, carried from the dark theme because that is the one that already looked right: each grip tone sits
//   a FIXED NUMBER OF LIGHTNESS STEPS from the splitter background, in whichever direction the background allows --
//   lighter on a dark ground, darker on a light one. Dark therefore renders as it always did; light becomes its
//   mirror. Hue and saturation are carried across from the background, so a tinted theme keeps its tint.
//
//   It also fixes the centring: Fusion places the 2 px grip at column offsets 4-5 of an 8 px handle, one pixel right
//   of centre. Painting it ourselves puts it at 3-4.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "style/tone.hpp"

#include <QSplitter>
#include <QSplitterHandle>

class QPaintEvent;

namespace vje
{
	// contrasting_tone -- the grip's tones come from style/tone.hpp, which the Code View's gutter and current-line bar
	// now share (EDITOR-07). Included here rather than forward-declared so tst_workspace_splitter, which pins the rule,
	// keeps seeing it through this header.

	//*****************************************************************************************************************
	// Class: WorkspaceSplitterHandle
	//*****************************************************************************************************************

	class WorkspaceSplitterHandle : public QSplitterHandle
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		WorkspaceSplitterHandle ( Qt::Orientation orientation, QSplitter* parent );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void paintEvent ( QPaintEvent* event ) override;
	};

	//*****************************************************************************************************************
	// Class: WorkspaceSplitter
	//*****************************************************************************************************************

	class WorkspaceSplitter : public QSplitter
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit WorkspaceSplitter ( Qt::Orientation orientation, QWidget* parent = nullptr );

		//=============================================================================================================
		// Overrides
		//=============================================================================================================

	protected:

		QSplitterHandle* createHandle () override;
	};
}

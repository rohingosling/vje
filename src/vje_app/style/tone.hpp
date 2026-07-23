//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   tone -- "a colour a fixed distance from this background, in whichever direction there is room for it".
//
//   THE RULE, AND WHY IT IS ONE RULE. Chrome that has to be visible against a themed surface -- the splitter grip
//   (STYLE-04), the Code View's gutter and current-line bar (EDITOR-07) -- cannot be specified as a colour, because the
//   surface it sits on moves between themes. Specified as a fixed lightness OFFSET it is still wrong, because an offset
//   that lifts a dark ground clear of its content runs off the end of the scale on a light one. Specified as a distance
//   applied AWAY from whichever end the ground is nearer, one number covers both themes and cannot be silently inert on
//   either.
//
//   That last failure is not hypothetical: the splitter grip was drawn with fixed translucent overlays and was clear on
//   the dark theme (+73 lightness) and invisible on the light one (+4) for a full phase, because nobody looked at the
//   light theme with the question in mind. Anything that would otherwise pick two colours by eye, one per theme, should
//   come through here instead.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>

namespace vje
{
	// A tone lightnessSteps away from ground: lighter when ground is dark, darker when it is light, clamped at the
	// ends of the scale. The ground's hue and saturation are preserved, so a tinted surface keeps its tint.

	QColor contrasting_tone ( const QColor& ground, int lightnessSteps );
}

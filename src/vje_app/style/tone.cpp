//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   tone implementation. See the header for why chrome contrast is expressed as a distance rather than as a colour.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/tone.hpp"

namespace vje
{
	namespace
	{
		// Where the lightness scale turns around. A ground below this is dark, so its chrome goes lighter; above it,
		// the chrome goes darker.

		constexpr int MID_LIGHTNESS = 128;
	}

	QColor contrasting_tone ( const QColor& ground, int lightnessSteps )
	{
		const int lightness = ground.lightness ();

		const int target = ( lightness < MID_LIGHTNESS ) ? qMin ( 255, lightness + lightnessSteps )
		                                                 : qMax (   0, lightness - lightnessSteps );

		// An achromatic ground reports hue -1, which QColor::fromHsl accepts and answers with a grey. Passing it
		// through rather than special-casing it keeps a tinted ground tinted -- the dark theme's #2D2D30 carries a
		// faint blue that its grip should carry too.

		return QColor::fromHsl ( ground.hslHue (), ground.hslSaturation (), target );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CodeTokenPalette implementation. See the header for why the token colours are literal and the chrome is derived.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/CodeTokenPalette.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QPalette>

namespace vje
{
	namespace
	{
		// Where a Base colour stops counting as light. The same turning point tone.cpp uses, for the same reason.

		constexpr int MID_LIGHTNESS = 128;

		//-------------------------------------------------------------------------------------------------------------
		// The two token families. Chosen to be FAMILIAR rather than derived -- a developer reading JSON in this view has
		// read JSON in an editor before, and matching that expectation is the whole value of a code palette (spec
		// section 2.9). These follow the VS Code light+ / dark+ JSON colouring.
		//
		// Member names and string VALUES are deliberately far apart in hue rather than being two shades of one colour:
		// telling a key from a value at a glance is the single most useful thing this colouring does, and two shades of
		// blue do not survive being read at a distance, on a projector, or by a user with reduced colour discrimination.
		//-------------------------------------------------------------------------------------------------------------

		CodeTokenPalette light_tokens ()
		{
			CodeTokenPalette tokens;

			tokens.memberName  = QColor ( 0x04, 0x51, 0xA5 );
			tokens.stringValue = QColor ( 0xA3, 0x15, 0x15 );
			tokens.number      = QColor ( 0x09, 0x86, 0x58 );
			tokens.literal     = QColor ( 0x00, 0x00, 0xFF );
			tokens.punctuation = QColor ( 0x3B, 0x3B, 0x3B );

			return tokens;
		}

		CodeTokenPalette dark_tokens ()
		{
			CodeTokenPalette tokens;

			tokens.memberName  = QColor ( 0x9C, 0xDC, 0xFE );
			tokens.stringValue = QColor ( 0xCE, 0x91, 0x78 );
			tokens.number      = QColor ( 0xB5, 0xCE, 0xA8 );
			tokens.literal     = QColor ( 0x56, 0x9C, 0xD6 );
			tokens.punctuation = QColor ( 0xD4, 0xD4, 0xD4 );

			return tokens;
		}
	}

	//=================================================================================================================
	// Factories
	//=================================================================================================================

	bool CodeTokenPalette::is_dark_surface ( const QPalette& palette )
	{
		// Base, not Window: these colours are read against the EDITOR's text background, which is the surface Base
		// names. On a light theme with a dark chrome (or the reverse) Window would answer the wrong question.

		return palette.color ( QPalette::Base ).lightness () < MID_LIGHTNESS;
	}

	CodeTokenPalette CodeTokenPalette::for_palette ( const QPalette& palette )
	{
		const QColor background = palette.color ( QPalette::Base );

		CodeTokenPalette resolved = is_dark_surface ( palette ) ? dark_tokens () : light_tokens ();

		resolved.gutterBackground      = contrasting_tone ( background, config::code::GUTTER_SURFACE_CONTRAST );
		resolved.gutterText            = contrasting_tone ( background, config::code::GUTTER_TEXT_CONTRAST );
		resolved.gutterCurrentText     = contrasting_tone ( background, config::code::GUTTER_CURRENT_TEXT_CONTRAST );
		resolved.currentLineBackground = contrasting_tone ( background, config::code::CURRENT_LINE_CONTRAST );

		return resolved;
	}
}

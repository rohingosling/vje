//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   dialog_surface -- the colours STYLE-15's dialog structure needs, stated once: the rule that closes a dialog's
//   content, the outline of a group box inside one (SET-01c), and the tone explanatory prose is dimmed to.
//
//   EACH IS A DISTANCE FROM THE SURFACE, not a colour, which is card_surface's shape and tone.hpp's rule. A dialog is
//   drawn on QPalette::Window under both themes and the two are at opposite ends of the lightness scale, so anything
//   named as a colour here would be right on one theme and wrong or invisible on the other -- the failure the splitter
//   grip shipped with for a whole phase (tone.hpp).
//
//   NOT A STYLESHEET, which is the other half of STYLE-15 and the reason this file exists at all. A stylesheet set on
//   a dialog puts QStyleSheetStyle into the paint path of its whole SUBTREE, and those subtrees hold the transfer
//   list, both grids and a CodeEditor that paints its own gutter. The application contains no setStyleSheet call and
//   that is worth keeping true (architecture.md section 9).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>

class QPalette;

namespace vje
{
	// The one-pixel rule closing a dialog's content, above its button row (STYLE-15).

	QColor dialog_rule ( const QPalette& palette );

	// The outline of a group box inside a dialog (SET-01c, dialogs/GroupBox). The same kind of line as the rule and,
	// today, the same distance -- stated as a function of its own so a box's outline and the rule are each named where
	// they are drawn, and can part company by one constant if the two ever should.
	//
	// It REPLACES the base style's frame rather than adjusting it: Fusion draws a group box from a fixed image, a
	// translucent grey line that lands 25 lightness levels below the light surface and only 14 above the dark one (read
	// from Qt 6.8.3 and 6.10.1's fusion_groupbox.png, composited on VJE's two window colours) -- uneven between the
	// themes, which is precisely what a distance from the surface cannot be.

	QColor dialog_group_frame ( const QPalette& palette );

	// Explanatory prose that is subordinate to the label naming it -- the XML import dialog's strategy description is
	// the first of them (section 2.11).
	//
	// DELIBERATELY NOT QPalette::PlaceholderText. That role's light value is an accepted sub-AA exception (spec
	// section 5, 2.61:1) and it is accepted for text that PROMPTS and is replaced by the user's first keystroke.
	// Prose is read instead, so it takes a tone that clears AA -- and keeping the two apart is the containment rule
	// the exception was accepted with, not a preference.

	QColor dialog_dimmed_prose ( const QPalette& palette );
}

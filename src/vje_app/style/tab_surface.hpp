//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   tab_surface -- the fill of one tab in the editor pane's strip, and of the tree pane's Explorer band (STYLE-13 /
//   STYLE-14), as a pure function of the palette and two booleans.
//
//   WHY THIS EXISTS RATHER THAN FUSION'S OWN TAB PAINTING. Fusion derives both tab fills from QPalette::Button and
//   always draws the UNSELECTED tab darker than the selected one. Against dark content that reads correctly -- the
//   selected tab is the lightest thing on the strip and stands out. Against WHITE content it is backwards: the selected
//   tab ends up nearer white than its neighbours, so the unselected tabs are the heavier, more prominent ones and the
//   active view is the hardest tab to pick out. No palette value can fix that, because one Button colour feeds both
//   fills through a fixed relationship.
//
//   THE RULE, WHICH IS THE SAME RULE THE REST OF THE CHROME FOLLOWS. Each surface sits a fixed lightness distance from
//   the CONTENT (QPalette::Base), applied away from whichever end of the scale the content is nearer -- style/tone.hpp.
//   So:
//
//     - the SELECTED tab stands FURTHEST from the content (lighter on a dark theme, darker on a light one);
//     - the UNSELECTED tabs sit nearer it;
//     - the whole strip recedes toward the content when the pane does not hold the keyboard (STYLE-14).
//
//   One rule generates all eight values. The four distances are the DARK theme's previously rendered fills measured
//   back into this form, so the dark theme is unchanged to within the content's own faint tint and the light theme
//   becomes its mirror.
//
//   Pure, so the whole colour scheme is assertable headlessly -- which matters more than usual here, because the defect
//   this replaces was invisible in one theme and only obvious in the other (the STYLE-04 grip
//   that shipped invisible on the light theme for a phase).
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
	// The fill for one tab. `selected` is the current tab of the strip (or, for PaneHeader, always true -- the band
	// wears the selected tab's surface so the two panes read as the same register). `paneFocused` is STYLE-14's
	// question: does the pane this chrome belongs to hold the keyboard?

	QColor tab_surface ( const QPalette& palette, bool selected, bool paneFocused );

	// The distance this surface sits from the content, in lightness steps. Exposed because it is what the tests assert
	// the ordering of -- comparing colours directly would have to know which theme it is in, which is exactly the
	// per-theme reasoning this rule exists to remove.

	int tab_surface_contrast ( bool selected, bool paneFocused );
}

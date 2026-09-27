//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tab_surface / band_surface -- the fill of one tab in the editor pane's strip, and of the tree pane's Explorer band
//   (STYLE-11 / 13 / 14), as pure functions of the palette and the focus state.
//
//   WHY THIS EXISTS RATHER THAN FUSION'S OWN TAB PAINTING. Fusion derives both tab fills from QPalette::Button and
//   always draws the UNSELECTED tab darker than the selected one. Against dark content that reads correctly -- the
//   selected tab is the lightest thing on the strip and stands out. Against WHITE content it is backwards: the selected
//   tab ends up nearer white than its neighbours, so the unselected tabs are the heavier, more prominent ones and the
//   active view is the hardest tab to pick out. No palette value can fix that, because one Button colour feeds both
//   fills through a fixed relationship.
//
//   THE RULE (revised at the 2026-07-24 review). Each surface sits a fixed lightness distance from the CONTENT
//   (QPalette::Base), applied away from whichever end of the scale the content is nearer -- style/tone.hpp. On that
//   footing:
//
//     - the UNSELECTED tabs are a STATIC FIELD: one shade per theme, the same whether or not the pane holds the
//       keyboard;
//     - only the SELECTED tab answers focus -- furthest from the content while its pane holds the keyboard, dropping
//       to a middle shade (still clear of the field) when it does not, so "which view am I on?" stays answerable in
//       BOTH focus states;
//     - the Explorer band keeps its own strong / receded pair, DECOUPLED from the selected tab -- which is what lets
//       the light strip's field match the receded band exactly while the unfocused active tab sits a step darker.
//
//   The distances are tuned PER THEME (the dark strip wants a quieter field than the light one) and live in
//   AppConfig.hpp as the chrome's hand-tuning dials; which set applies is decided from the content itself, the same
//   way the tone rule decides its direction.
//
//   Pure, so the whole colour scheme is assertable headlessly -- which matters more than usual here, because the
//   defect the original rule replaced was invisible in one theme and only obvious in the other.
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
	// The fill for one tab of the editor strip. `selected` is the strip's current tab; `paneFocused` is STYLE-14's
	// question: does the pane this chrome belongs to hold the keyboard? An unselected tab answers with the theme's
	// static field, focused or not.

	QColor tab_surface ( const QPalette& palette, bool selected, bool paneFocused );

	// The Explorer band's fill (STYLE-13): the strong shade while the tree holds the keyboard, the receded one
	// otherwise -- one pair, both themes, independent of the tab strip's selected surface.

	QColor band_surface ( const QPalette& palette, bool paneFocused );

	// The distances the surfaces sit from the content, in lightness steps. Exposed because the tests assert the
	// ORDERING of these -- comparing colours directly would have to know which side of the content each theme paints
	// on, which is exactly the per-theme reasoning the tone rule exists to remove. `contentIsDark` selects the theme's
	// value set (the two strips are tuned separately).

	int tab_surface_contrast  ( bool contentIsDark, bool selected, bool paneFocused );
	int band_surface_contrast ( bool paneFocused );
}

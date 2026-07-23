//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   fixed_font -- a font whose characters are all the same width, VERIFIED rather than requested.
//
//   WHY THIS IS NOT ONE LINE. The obvious call is QFontDatabase::systemFont(QFontDatabase::FixedFont), and on this
//   Windows machine it answers with the family literally named "monospace" -- a generic ALIAS, not a font -- which the
//   matcher then resolves to "Agency FB", a narrow PROPORTIONAL face. It looks like a deliberate choice of a
//   condensed font and it is not fixed-width at all. QFont::setStyleHint(Monospace) does not rescue it, and
//   QFontInfo::fixedPitch() reports the resolved family's metadata, which is only as trustworthy as the font's own
//   flags.
//
//   Both places this matters are places where the width IS the correctness:
//
//     - the Text View, where TextViewRenderer aligns separators and draws table rules by COUNTING CHARACTERS, so a
//       proportional face leaves every aligned column ragged and every border broken (EDITOR-06);
//     - the Code View, where indentation has to line up down the page for nesting to be readable at all (EDITOR-07).
//
//   So the choice is MEASURED: candidate families are tried in order and the first whose glyphs actually advance by
//   equal amounts wins. Measuring is the only check that cannot be fooled by a font's own metadata, which is the same
//   reasoning behind reading rendered pixels rather than palette roles for a colour claim.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QFont>

namespace vje
{
	// A fixed-width font at the system's own fixed-font size. Falls back to the system's answer if nothing on the
	// machine measures fixed -- a document rendered in a proportional font is still better than no document.

	QFont monospace_font ();

	// Does this font actually advance every glyph by the same width? Exposed because it is the property callers care
	// about, and because it is what the tests assert rather than QFontInfo::fixedPitch().

	bool measures_fixed_width ( const QFont& font );
}

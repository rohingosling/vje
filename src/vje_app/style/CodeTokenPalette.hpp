//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CodeTokenPalette -- every colour the Code View decides for itself (EDITOR-07): the five JSON token colours, and the
//   gutter and current-line surfaces.
//
//   THE TOKEN COLOURS ARE A DELIBERATE EXCEPTION to the themed-brush rule, and the spec says so outright: a
//   code palette communicates TOKEN KINDS, and the semantic UI brushes have nothing to say about the difference between
//   a member name and a string value. They are therefore familiar editor colours (the VS Code light / dark families)
//   rather than anything derived from QPalette.
//
//   THE GUTTER AND CURRENT-LINE SURFACES ARE NOT. Those are chrome, and EDITOR-07 asks only that they be "subtly
//   contrasting", so they are DERIVED from the editor's own background by a fixed number of lightness steps, applied
//   away from whichever end of the scale that background is nearer -- the same rule the splitter grip follows
//   (STYLE-04). One rule covers both themes, so a theme change cannot leave the gutter invisible on one of them, which
//   is exactly how the splitter grip went unnoticed for a phase.
//
//   WHY IT TAKES A QPalette AND NOT THE ThemeService. Light or dark is a MEASUREMENT here -- the lightness of the
//   editor's Base colour -- not a setting to be looked up. Reading it from the widget's own palette means the class
//   needs no injected service, tracks any future third theme without being told about it, and can be tested by handing
//   it a palette.
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
	//*****************************************************************************************************************
	// Class: CodeTokenPalette
	//*****************************************************************************************************************

	class CodeTokenPalette
	{
		//=============================================================================================================
		// Factories
		//=============================================================================================================

	public:

		// Resolve against a widget's palette. Dark is decided by the lightness of QPalette::Base -- the editor's own
		// text background, which is the surface these colours are read against.

		static CodeTokenPalette for_palette ( const QPalette& palette );

		static bool is_dark_surface ( const QPalette& palette );

		//=============================================================================================================
		// Data Members -- JSON token colours (the spec's deliberate exception).
		//=============================================================================================================

	public:

		QColor memberName;      // The "key" of a member.
		QColor stringValue;     // A string that is a VALUE rather than a name.
		QColor number;
		QColor literal;         // true / false / null.
		QColor punctuation;     // { } [ ] : ,

		//=============================================================================================================
		// Data Members -- chrome, derived from the editor's background.
		//=============================================================================================================

	public:

		QColor gutterBackground;
		QColor gutterText;              // Line numbers other than the caret's.
		QColor gutterCurrentText;       // The caret's line number, lifted out of the column.
		QColor currentLineBackground;   // The current-line bar (EDITOR-07).
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonEscapeValidator -- the rule attached to a string editor while SET-03's notation is ESCAPED: the text must
//   decode. "a\tb" does; "a\qb" does not, and neither does a line ending in a lone backslash.
//
//   IT NEVER RETURNS Invalid, and that is the whole design decision here. Invalid refuses the KEYSTROKE, which is right
//   for a number -- there is no point letting a user type a character that can never be part of a valid one -- and wrong
//   here, because the way to write a literal backslash-t is to type a backslash first, and refusing the second
//   character of every escape as it is typed would make the notation unusable. So anything
//   that does not yet decode is Intermediate: the typing is allowed, and it is the COMMIT that is refused, with the
//   caret held in the cell and the reason in the status bar. That reuses VAL-03's existing path exactly -- the delegate
//   already declines to close an editor whose input is not Acceptable.
//
//   It is deliberately NOT installed in Decoded mode: there the field is raw text, every character is legal, and a
//   backslash means a backslash.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QValidator>

namespace vje
{
	//*****************************************************************************************************************
	// Class: JsonEscapeValidator
	//*****************************************************************************************************************

	class JsonEscapeValidator : public QValidator
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonEscapeValidator ( QObject* parent = nullptr );

		//=============================================================================================================
		// QValidator
		//=============================================================================================================

	public:

		State validate ( QString& input, int& position ) const override;

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Why a commit was refused, for the status bar (VAL-04). Named here so the delegate does not compose the
		// wording and the test does not match on a literal.

		static QString refusal_reason ();
	};
}

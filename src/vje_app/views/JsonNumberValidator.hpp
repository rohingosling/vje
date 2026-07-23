//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonNumberValidator -- the QValidator behind a number field or cell (VAL-03, EDITOR-02 / 03).
//
//   WHY A VALIDATOR RATHER THAN A COMMIT-TIME CHECK. VAL-03 has to reject "12abc" without rejecting "-", "1.", or "1e"
//   -- each of which is nonsense on its own but is the halfway state of typing a perfectly good number. That is exactly
//   the Acceptable / Intermediate / Invalid distinction QValidator exists for, and expressing it here buys the specified
//   behaviour for free: Qt's own editor plumbing refuses to commit a QLineEdit whose input is not Acceptable, which IS
//   "a rejected commit keeps the caret in the errored cell" (EDITOR-03).
//
//   ACCEPTABLE is delegated to Validator::is_valid_number, so "a JSON number" means exactly what the parser accepts --
//   one definition across the codebase, no second grammar to keep in step.
//
//   INTERMEDIATE is any text that could still GROW into a JSON number: an optional sign, an optional integer part, an
//   optional fraction, an optional exponent, each allowed to be incomplete. Note what this deliberately excludes -- a
//   leading-zero run such as "01" is Invalid rather than Intermediate, because no valid JSON number begins that way, so
//   the keystroke is refused at the point it is typed rather than at commit.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QRegularExpression>
#include <QValidator>

namespace vje
{
	//*****************************************************************************************************************
	// Class: JsonNumberValidator
	//*****************************************************************************************************************

	class JsonNumberValidator : public QValidator
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonNumberValidator ( QObject* parent = nullptr );

		//=============================================================================================================
		// QValidator
		//=============================================================================================================

	public:

		State validate ( QString& text, int& position ) const override;

		void fixup ( QString& text ) const override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		// The "could still become a JSON number" pattern. Held as a member so it is compiled once rather than on every
		// keystroke.

		QRegularExpression partialNumberPattern;
	};
}

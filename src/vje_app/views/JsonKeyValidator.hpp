//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonKeyValidator -- the VAL-02 duplicate-key rule as a QValidator, so an in-place rename cannot be COMMITTED onto a
//   key the object already has (EDIT-02).
//
//   WHY INTERMEDIATE AND NEVER INVALID, which is the whole design. Invalid input is refused keystroke by keystroke:
//   QLineEdit simply will not accept the character. That is right for a number (JsonNumberValidator, VAL-03), where
//   every prefix of a valid number is itself acceptable input. It is wrong for a key, because the route to a legal name
//   often passes through an illegal one -- renaming "nam" to "name" in an object that already has "nam" would be
//   impossible if each keystroke were judged final.
//
//   Intermediate says instead: this is fine to be typing, but not fine to stop on. QLineEdit accepts every keystroke,
//   and the delegate's commit path already refuses to close an editor whose input is not acceptable -- so the caret
//   stays in the errored cell exactly as EDITOR-03 specifies for a rejected value, and by the same mechanism.
//
//   The rule is duplicated deliberately: UndoController rejects a colliding rename regardless (EDIT-02). This validator
//   is the interaction, not the guarantee.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QStringList>
#include <QValidator>

namespace vje
{
	//*****************************************************************************************************************
	// Class: JsonKeyValidator
	//*****************************************************************************************************************

	class JsonKeyValidator : public QValidator
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// `rivalKeys` are the other keys in the same object -- the ones this rename must not land on. The key being
		// renamed is not among them, so leaving the name unchanged is always acceptable.

		explicit JsonKeyValidator ( QStringList rivalKeys, QObject* parent = nullptr );

		//=============================================================================================================
		// QValidator
		//=============================================================================================================

	public:

		State validate ( QString& input, int& position ) const override;

		//=============================================================================================================
		// Fields
		//=============================================================================================================

	private:

		QStringList rivals;
	};
}

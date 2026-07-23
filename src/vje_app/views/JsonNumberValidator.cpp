//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonNumberValidator implementation. See the header for why the Intermediate state carries the weight here.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonNumberValidator.hpp"

#include <vje_core/services/Validator.hpp>

namespace vje
{
	namespace
	{
		// Every prefix of a JSON number, with each part allowed to be absent or incomplete: an optional sign, an
		// optional integer part (with JSON's no-leading-zeros rule intact), an optional fraction whose digits may not
		// have been typed yet, and an optional exponent likewise.
		//
		// The anchors matter -- an unanchored match would accept "12abc" on the strength of its "12".

		const char* const PARTIAL_JSON_NUMBER_PATTERN = R"(^-?(0|[1-9][0-9]*)?(\.[0-9]*)?([eE][+-]?[0-9]*)?$)";
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonNumberValidator::JsonNumberValidator ( QObject* parent )
		: QValidator ( parent )
		, partialNumberPattern ( QString::fromLatin1 ( PARTIAL_JSON_NUMBER_PATTERN ) )
	{
	}

	//=================================================================================================================
	// QValidator
	//=================================================================================================================

	QValidator::State JsonNumberValidator::validate ( QString& text, int& position ) const
	{
		Q_UNUSED ( position );

		const QString candidate = text.trimmed ();

		// Empty is Intermediate, never Invalid: clearing the field to retype it is a normal gesture, and refusing the
		// deletion would trap the user in the value they started with.

		if ( candidate.isEmpty () )
		{
			return Intermediate;
		}

		if ( Validator::is_valid_number ( candidate ) )
		{
			return Acceptable;
		}

		return partialNumberPattern.match ( candidate ).hasMatch () ? Intermediate : Invalid;
	}

	void JsonNumberValidator::fixup ( QString& text ) const
	{
		// Surrounding whitespace is the one flaw worth repairing silently -- it is invisible, and it is what a paste
		// from another application most often brings with it.

		text = text.trimmed ();
	}
}

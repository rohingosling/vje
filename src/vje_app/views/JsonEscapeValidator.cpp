//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonEscapeValidator implementation. See the header for why it never answers Invalid.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonEscapeValidator.hpp"

#include <vje_core/services/json_escapes.hpp>

namespace vje
{
	JsonEscapeValidator::JsonEscapeValidator ( QObject* parent )
		: QValidator ( parent )
	{
	}

	QValidator::State JsonEscapeValidator::validate ( QString& input, int& position ) const
	{
		Q_UNUSED ( position );

		QString decoded;

		// Acceptable exactly when the text decodes. Everything else -- an unfinished escape, a meaningless one -- is
		// Intermediate rather than Invalid, so the user may type through it and the commit is what refuses.

		return json_escapes::decode ( input, decoded ) ? Acceptable : Intermediate;
	}

	QString JsonEscapeValidator::refusal_reason ()
	{
		return tr ( "Not a valid escape sequence. Use \\\\ for a backslash, or one of \\\" \\/ \\b \\f \\n \\r \\t \\uXXXX." );
	}
}

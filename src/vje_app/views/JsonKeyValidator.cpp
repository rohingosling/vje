//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonKeyValidator implementation. See the header for why a colliding key is Intermediate and never Invalid.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonKeyValidator.hpp"

#include <utility>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonKeyValidator::JsonKeyValidator ( QStringList rivalKeys, QObject* parent )
		: QValidator ( parent )
		, rivals     ( std::move ( rivalKeys ) )
	{
	}

	//=================================================================================================================
	// QValidator
	//=================================================================================================================

	QValidator::State JsonKeyValidator::validate ( QString& input, int& position ) const
	{
		Q_UNUSED ( position );

		// Every key is compared EXACTLY -- no trimming, no case folding. JSON member names are byte sequences, and
		// " name" is a different member from "name"; treating them as the same here would refuse a rename the document
		// model would have accepted.

		return rivals.contains ( input ) ? QValidator::Intermediate : QValidator::Acceptable;
	}
}

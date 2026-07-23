//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   code_indentation implementation. See the header for why outdent is tolerant of whitespace the profile did not
//   produce and indent is not.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/code_indentation.hpp"

#include <QStringList>

namespace vje
{
	namespace
	{
		// The profile's indentSize is only meaningful for Spaces, but an outdent has to answer "how many leading spaces
		// is one level?" under BOTH profiles -- a Tabs document can still contain space-indented lines somebody pasted
		// in. The stored size is the best available answer either way.

		int space_width ( const FormatProfile& profile )
		{
			return ( profile.indentSize >= 1 ) ? profile.indentSize : 1;
		}
	}

	QString indent_unit ( const FormatProfile& profile )
	{
		if ( profile.indent == IndentKind::Tabs )
		{
			return QStringLiteral ( "\t" );
		}

		return QString ( space_width ( profile ), QLatin1Char ( ' ' ) );
	}

	int outdent_width ( const QString& line, const FormatProfile& profile )
	{
		if ( line.isEmpty () )
		{
			return 0;
		}

		// A leading tab is one level whatever the profile says, because it is one level to whoever typed it.

		if ( line.at ( 0 ) == QLatin1Char ( '\t' ) )
		{
			return 1;
		}

		const int budget = space_width ( profile );

		int spaces = 0;

		while ( ( spaces < budget ) && ( spaces < line.length () ) && ( line.at ( spaces ) == QLatin1Char ( ' ' ) ) )
		{
			++spaces;
		}

		return spaces;
	}

	QString indent_block ( const QString& block, const FormatProfile& profile )
	{
		const QString unit = indent_unit ( profile );

		QStringList lines = block.split ( QLatin1Char ( '\n' ) );

		for ( QString& line : lines )
		{
			if ( !line.isEmpty () )
			{
				line.prepend ( unit );
			}
		}

		return lines.join ( QLatin1Char ( '\n' ) );
	}

	QString outdent_block ( const QString& block, const FormatProfile& profile )
	{
		QStringList lines = block.split ( QLatin1Char ( '\n' ) );

		for ( QString& line : lines )
		{
			// Per line rather than per block: a block with one flush line among indented ones outdents the rest and
			// leaves that one where it is, which is what a user dragging a selection over a whole object expects.

			line.remove ( 0, outdent_width ( line, profile ) );
		}

		return lines.join ( QLatin1Char ( '\n' ) );
	}
}

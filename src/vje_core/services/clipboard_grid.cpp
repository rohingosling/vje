//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   clipboard_grid -- see the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/clipboard_grid.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <algorithm>

namespace vje
{
	namespace
	{
		constexpr QChar TAB             = QLatin1Char ( '\t' );
		constexpr QChar LINE_FEED       = QLatin1Char ( '\n' );
		constexpr QChar CARRIAGE_RETURN = QLatin1Char ( '\r' );
		constexpr QChar QUOTE           = QLatin1Char ( '"' );

		// The text without its one trailing line break, whichever of the three spellings it is.

		QString without_trailing_break ( const QString& text )
		{
			if ( text.endsWith ( QStringLiteral ( "\r\n" ) ) )
			{
				return text.left ( text.length () - 2 );
			}

			if ( text.endsWith ( LINE_FEED ) || text.endsWith ( CARRIAGE_RETURN ) )
			{
				return text.left ( text.length () - 1 );
			}

			return text;
		}

		//-------------------------------------------------------------------------------------------------------------
		// Split the text into rows of fields. Returns false where quoting was on and a quoted field never closed -- the
		// caller then reads the text again with it off.
		//-------------------------------------------------------------------------------------------------------------

		bool split ( const QString& text, bool honourQuotes, std::vector<QStringList>* rows )
		{
			rows->clear ();

			QStringList row;
			QString     field;
			bool        inQuotes   = false;
			bool        fieldStart = true;

			const int length = text.length ();
			int       index  = 0;

			while ( index < length )
			{
				const QChar character = text.at ( index );

				if ( inQuotes )
				{
					if ( character != QUOTE )
					{
						field.append ( character );
						index += 1;
					}
					else if ( ( index + 1 < length ) && ( text.at ( index + 1 ) == QUOTE ) )
					{
						field.append ( QUOTE );                  // A doubled quote is one quote.
						index += 2;
					}
					else
					{
						inQuotes = false;                         // The closing quote.
						index   += 1;
					}

					continue;
				}

				if ( ( character == QUOTE ) && fieldStart && honourQuotes )
				{
					inQuotes   = true;
					fieldStart = false;
					index     += 1;
				}
				else if ( character == TAB )
				{
					row.append ( field );
					field.clear ();
					fieldStart = true;
					index     += 1;
				}
				else if ( ( character == LINE_FEED ) || ( character == CARRIAGE_RETURN ) )
				{
					row.append ( field );
					rows->push_back ( row );
					row.clear ();
					field.clear ();
					fieldStart = true;

					const bool crLf = ( character == CARRIAGE_RETURN ) && ( index + 1 < length )
					               && ( text.at ( index + 1 ) == LINE_FEED );

					index += crLf ? 2 : 1;
				}
				else
				{
					field.append ( character );
					fieldStart = false;
					index     += 1;
				}
			}

			if ( inQuotes )
			{
				return false;
			}

			// The last row always exists: the trailing break was dropped before splitting, so the text ends inside it.

			row.append ( field );
			rows->push_back ( row );

			return true;
		}
	}

	std::optional<std::vector<QStringList>> clipboard_grid::parse ( const QString& text )
	{
		// One JSON value, however many lines it spans, is one value.

		if ( text.isEmpty () || JsonParser::parse ( text ).ok )
		{
			return std::nullopt;
		}

		const QString body = without_trailing_break ( text );

		const bool hadBreak      = ( body.length () != text.length () );
		const bool hasSeparators = body.contains ( TAB ) || body.contains ( LINE_FEED ) || body.contains ( CARRIAGE_RETURN );

		if ( !hadBreak && !hasSeparators )
		{
			return std::nullopt;
		}

		std::vector<QStringList> rows;

		if ( !split ( body, true, &rows ) )
		{
			split ( body, false, &rows );
		}

		return rows;
	}

	int clipboard_grid::width ( const std::vector<QStringList>& rows )
	{
		int widest = 0;

		for ( const QStringList& row : rows )
		{
			widest = std::max ( widest, static_cast<int> ( row.size () ) );
		}

		return widest;
	}
}

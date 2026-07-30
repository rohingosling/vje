//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_escapes implementation. See the header for why there is one table and why the mode is the notation.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/json_escapes.hpp>

namespace vje
{
	namespace json_escapes
	{
		bool is_control ( QChar character )
		{
			return character.unicode () < 0x20;
		}

		QString escape ( QStringView value )
		{
			QString escaped;

			escaped.reserve ( value.size () );

			for ( const QChar character : value )
			{
				switch ( character.unicode () )
				{
					case u'"':  escaped += QLatin1String ( "\\\"" ); break;
					case u'\\': escaped += QLatin1String ( "\\\\" ); break;
					case u'\b': escaped += QLatin1String ( "\\b"  ); break;
					case u'\f': escaped += QLatin1String ( "\\f"  ); break;
					case u'\n': escaped += QLatin1String ( "\\n"  ); break;
					case u'\r': escaped += QLatin1String ( "\\r"  ); break;
					case u'\t': escaped += QLatin1String ( "\\t"  ); break;

					default:
					{
						// The solidus is deliberately NOT escaped. RFC 8259 permits "\/" and requires nothing, and
						// escaping it would make every file path in a document unreadable.

						if ( is_control ( character ) )
						{
							escaped += QString::asprintf ( "\\u%04x", character.unicode () );
						}
						else
						{
							escaped += character;
						}

						break;
					}
				}
			}

			return escaped;
		}

		bool decode ( QStringView body, QString& out, int* errorOffset )
		{
			out.clear ();
			out.reserve ( body.size () );

			const auto fail = [ &errorOffset ] ( int offset )
			{
				if ( errorOffset != nullptr )
				{
					*errorOffset = offset;
				}

				return false;
			};

			int index = 0;

			while ( index < body.size () )
			{
				const QChar character = body [ index ];

				// Everything that is not an escape passes through, INCLUDING a raw control character: a tab pasted into
				// an escaped field means a tab (see the header). It reappears escaped once committed.

				if ( character != QLatin1Char ( '\\' ) )
				{
					out.append ( character );

					++index;

					continue;
				}

				// A trailing lone backslash: unfinished rather than wrong, but it cannot be committed either way.

				if ( index + 1 >= body.size () )
				{
					return fail ( index );
				}

				const QChar escape = body [ index + 1 ];

				switch ( escape.unicode () )
				{
					case u'"':  out.append ( QLatin1Char ( '"'  ) ); index += 2; break;
					case u'\\': out.append ( QLatin1Char ( '\\' ) ); index += 2; break;
					case u'/':  out.append ( QLatin1Char ( '/'  ) ); index += 2; break;
					case u'b':  out.append ( QChar ( u'\b' ) );      index += 2; break;
					case u'f':  out.append ( QChar ( u'\f' ) );      index += 2; break;
					case u'n':  out.append ( QChar ( u'\n' ) );      index += 2; break;
					case u'r':  out.append ( QChar ( u'\r' ) );      index += 2; break;
					case u't':  out.append ( QChar ( u'\t' ) );      index += 2; break;

					case u'u':
					{
						// \uXXXX -- four hex digits, checked rather than assumed. Surrogate pairs fall out naturally:
						// each half becomes one UTF-16 code unit, and two consecutive halves form the intended code
						// point in the QString.

						if ( index + 6 > body.size () )
						{
							return fail ( index );
						}

						const QStringView digits = body.sliced ( index + 2, 4 );

						for ( const QChar digit : digits )
						{
							if ( !isxdigit ( digit.toLatin1 () ) )
							{
								return fail ( index );
							}
						}

						out.append ( QChar ( digits.toString ().toUShort ( nullptr, 16 ) ) );

						index += 6;

						break;
					}

					default:
					{
						// A completed but meaningless escape -- "\q". Reported so the caller can refuse the commit and
						// say where.

						return fail ( index );
					}
				}
			}

			return true;
		}

		QString flatten ( QStringView value )
		{
			QString flattened;

			flattened.reserve ( value.size () );

			for ( const QChar character : value )
			{
				if ( !is_control ( character ) )
				{
					flattened.append ( character );
				}
			}

			return flattened;
		}
	}

	//=================================================================================================================
	// The display policy (SET-03)
	//=================================================================================================================

	QString string_display_text ( const QString& value, StringDisplay mode )
	{
		switch ( mode )
		{
			case StringDisplay::Escaped:   return json_escapes::escape ( value );
			case StringDisplay::Decoded:   return value;
			case StringDisplay::Flattened: return json_escapes::flatten ( value );
		}

		return value;
	}

	QString string_edit_text ( const QString& value, StringDisplay mode )
	{
		// Flattened falls back to Escaped: it removes characters, so it cannot be its own edit notation without every
		// edit silently destroying them.

		return mode_edits_in_escaped_notation ( mode ) ? json_escapes::escape ( value ) : value;
	}

	bool string_commit_value ( const QString& text, StringDisplay mode, QString& outValue, int* errorOffset )
	{
		if ( !mode_edits_in_escaped_notation ( mode ) )
		{
			// Raw text: what was typed IS the value, so there is no notation to get wrong.

			outValue = text;

			return true;
		}

		return json_escapes::decode ( text, outValue, errorOffset );
	}

	bool mode_edits_in_escaped_notation ( StringDisplay mode )
	{
		return mode != StringDisplay::Decoded;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   json_text_index implementation -- a recursive-descent walk over JsonLexer's token stream that records a line per
//   node and builds nothing else. See the header for why the index is derived from the text rather than the document.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/json_text_index.hpp"

#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/JsonLexer.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QStringList>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// The scan's state. The path is carried as DECODED tokens, exactly as JsonPointer holds them, so recording a
		// node is a join rather than a conversion.
		//
		// The depth guard is JsonParser's, for the same reason: this walk recurses on nesting, and the text is
		// arbitrary user input.
		//-------------------------------------------------------------------------------------------------------------

		class Scanner
		{
		public:

			explicit Scanner ( const QString& text )
				: lexer ( text )
			{
				advance ();
			}

			void scan_document ()
			{
				scan_value ( 0 );
			}

			PointerLineIndex take_index ()
			{
				return std::move ( index );
			}

		private:

			void advance ()
			{
				currentToken = lexer.next ();
			}

			void record_current_path ( int line )
			{
				index.insert ( JsonPointer::from_tokens ( path ).to_string (), line );
			}

			// Scan one value, recording the line it starts on against the path currently held. Returns false as soon as
			// anything unexpected is met, which unwinds the whole scan -- see the header on tolerance.

			bool scan_value ( int depth )
			{
				if ( depth > JsonParser::MAX_DEPTH )
				{
					return false;
				}

				// Recorded only once the token is known to START a value. Recording first and dispatching afterwards is
				// a token shorter and quietly wrong: empty input would map the ROOT pointer to line 1, so a caller
				// asking where the document begins would get an answer for a document that is not there.

				switch ( currentToken.kind )
				{
					case JsonTokenKind::BeginObject:
					case JsonTokenKind::BeginArray:
					case JsonTokenKind::String:
					case JsonTokenKind::Number:
					case JsonTokenKind::True:
					case JsonTokenKind::False:
					case JsonTokenKind::Null:
					{
						record_current_path ( currentToken.line );

						break;
					}

					default:
					{
						return false;
					}
				}

				switch ( currentToken.kind )
				{
					case JsonTokenKind::BeginObject: return scan_object ( depth );
					case JsonTokenKind::BeginArray:  return scan_array  ( depth );

					case JsonTokenKind::String:
					case JsonTokenKind::Number:
					case JsonTokenKind::True:
					case JsonTokenKind::False:
					case JsonTokenKind::Null:
					{
						advance ();

						return true;
					}

					default:
					{
						return false;
					}
				}
			}

			bool scan_object ( int depth )
			{
				advance ();                                        // Past '{'.

				if ( currentToken.kind == JsonTokenKind::EndObject )
				{
					advance ();

					return true;
				}

				for ( ;; )
				{
					if ( currentToken.kind != JsonTokenKind::String )
					{
						return false;
					}

					// The member's line is its KEY's line, captured before the value is scanned -- under Allman the
					// value's opening brace is on the NEXT line, and the key is what names the node.

					const int     keyLine = currentToken.line;
					const QString key     = JsonParser::decode_string ( lexer.text_of ( currentToken ) );

					advance ();

					if ( currentToken.kind != JsonTokenKind::NameSeparator )
					{
						return false;
					}

					advance ();

					path.append ( key );

					// scan_value records the VALUE's own start line; overwriting it with the key's line afterwards is
					// what makes the member's row, rather than its value, the thing the pointer names.

					const bool valueScanned = scan_value ( depth + 1 );

					if ( valueScanned )
					{
						record_current_path ( keyLine );
					}

					path.removeLast ();

					if ( !valueScanned )
					{
						return false;
					}

					if ( currentToken.kind == JsonTokenKind::ValueSeparator )
					{
						advance ();

						continue;
					}

					if ( currentToken.kind == JsonTokenKind::EndObject )
					{
						advance ();

						return true;
					}

					return false;
				}
			}

			bool scan_array ( int depth )
			{
				advance ();                                        // Past '['.

				if ( currentToken.kind == JsonTokenKind::EndArray )
				{
					advance ();

					return true;
				}

				for ( int elementIndex = 0; ; ++elementIndex )
				{
					path.append ( QString::number ( elementIndex ) );

					const bool elementScanned = scan_value ( depth + 1 );

					path.removeLast ();

					if ( !elementScanned )
					{
						return false;
					}

					if ( currentToken.kind == JsonTokenKind::ValueSeparator )
					{
						advance ();

						continue;
					}

					if ( currentToken.kind == JsonTokenKind::EndArray )
					{
						advance ();

						return true;
					}

					return false;
				}
			}

			JsonLexer        lexer;
			JsonToken        currentToken;
			QStringList      path;                                 // Decoded tokens to the current position.
			PointerLineIndex index;
		};
	}

	PointerLineIndex build_pointer_line_index ( const QString& jsonText )
	{
		Scanner scanner ( jsonText );

		scanner.scan_document ();

		return scanner.take_index ();
	}

	int line_for_pointer ( const PointerLineIndex& index, const JsonPointer& pointer )
	{
		return index.value ( pointer.to_string (), 0 );
	}
}

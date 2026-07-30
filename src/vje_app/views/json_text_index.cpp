//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_text_index implementation -- a recursive-descent walk over JsonLexer's token stream that records a line SPAN
//   per node and builds nothing else. See the header for why the index is derived from the text rather than the
//   document, and why both ends of the span are needed.
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

			PointerSpanIndex take_index ()
			{
				return std::move ( index );
			}

		private:

			void advance ()
			{
				// The line of the token about to be left behind. After a value has been scanned this is the line of the
				// last token that belonged to it -- its closing brace, bracket, or the scalar itself -- which is the only
				// place the span's far end can come from.

				lastConsumedLine = currentToken.line;

				currentToken = lexer.next ();
			}

			// Recorded in two steps, and deliberately so. The START goes in before the value is dispatched, so a node
			// whose scan then fails halfway (a syntax error inside it) still answers "where does it begin?" -- which is
			// the tolerance the header promises. The END is written only once the value has been scanned through.

			void record_current_start ( int line )
			{
				index.insert ( JsonPointer::from_tokens ( path ).to_string (), LineSpan { line, line } );
			}

			void record_current_span ( int firstLine, int lastLine )
			{
				index.insert ( JsonPointer::from_tokens ( path ).to_string (), LineSpan { firstLine, lastLine } );
			}

			// Scan one value, recording the lines it spans against the path currently held. Returns false as soon as
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

				const int startLine = currentToken.line;

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
						record_current_start ( startLine );

						break;
					}

					default:
					{
						return false;
					}
				}

				bool scanned = false;

				switch ( currentToken.kind )
				{
					case JsonTokenKind::BeginObject:
					{
						scanned = scan_object ( depth );

						break;
					}

					case JsonTokenKind::BeginArray:
					{
						scanned = scan_array ( depth );

						break;
					}

					case JsonTokenKind::String:
					case JsonTokenKind::Number:
					case JsonTokenKind::True:
					case JsonTokenKind::False:
					case JsonTokenKind::Null:
					{
						advance ();

						scanned = true;

						break;
					}

					default:
					{
						return false;
					}
				}

				// The far end, now that the value has been scanned through. A failed scan leaves the degenerate start-only
				// span already recorded above.

				if ( scanned )
				{
					record_current_span ( startLine, lastConsumedLine );
				}

				return scanned;
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

					// scan_value records the VALUE's own span; widening its start back to the key's line afterwards is
					// what makes the member's row, rather than its value, the thing the pointer names. The far end is
					// the value's, which under Allman is the line its closing brace is on.

					const bool valueScanned = scan_value ( depth + 1 );

					if ( valueScanned )
					{
						record_current_span ( keyLine, lastConsumedLine );
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
			int              lastConsumedLine = 0;                 // The line of the token most recently left behind.
			QStringList      path;                                 // Decoded tokens to the current position.
			PointerSpanIndex index;
		};
	}

	PointerSpanIndex build_pointer_span_index ( const QString& jsonText )
	{
		Scanner scanner ( jsonText );

		scanner.scan_document ();

		return scanner.take_index ();
	}

	int line_for_pointer ( const PointerSpanIndex& index, const JsonPointer& pointer )
	{
		return index.value ( pointer.to_string (), LineSpan () ).firstLine;
	}

	JsonPointer pointer_at_line ( const PointerSpanIndex& index, int line, bool* outFound )
	{
		if ( outFound != nullptr )
		{
			*outFound = false;
		}

		// The DEEPEST containing span, found by width: a child's span is contained in its parent's, so the narrowest one
		// that still covers the line is the innermost node that owns it. The tie-break is the longer pointer text, which
		// is the deeper node -- two nodes cannot share a span under this formatter, but a tie-break costs one comparison
		// and does not depend on that staying true.

		QString bestPointerText;
		int     bestWidth = 0;
		bool    found     = false;

		for ( auto entry = index.constBegin (); entry != index.constEnd (); ++entry )
		{
			const LineSpan& span = entry.value ();

			if ( ( line < span.firstLine ) || ( line > span.lastLine ) )
			{
				continue;
			}

			const int width = span.lastLine - span.firstLine;

			const bool better = !found
			                 || ( width < bestWidth )
			                 || ( ( width == bestWidth ) && ( entry.key ().size () > bestPointerText.size () ) );

			if ( better )
			{
				bestPointerText = entry.key ();
				bestWidth       = width;
				found           = true;
			}
		}

		if ( !found )
		{
			return JsonPointer ();
		}

		if ( outFound != nullptr )
		{
			*outFound = true;
		}

		return JsonPointer::parse ( bestPointerText );
	}
}

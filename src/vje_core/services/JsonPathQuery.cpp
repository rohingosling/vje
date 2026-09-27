//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPathQuery implementation -- see the header for the design. This file holds three things in order: the file-local
//   helpers, the recursive-descent Parser (the whole grammar, in one place), and the evaluator.
//
//   THE PARSER NEVER THROWS AND NEVER PARTIALLY CONSUMES ON FAILURE. Every parse_* function answers a bool and leaves
//   the first failure in `error`; once `failed` is set nothing else is reported, so the message the user sees is the
//   FIRST thing that went wrong rather than whatever the recovery ran into afterwards.
//
//   POSITIONS ARE CHARACTER OFFSETS INTO THE QUERY TEXT, because that is what the query box can select. They are taken
//   BEFORE the offending token is consumed wherever the token is the problem, and after it where its content is.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonPathQuery.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/json_escapes.hpp>

#include <QtGlobal>

#include <unordered_set>

namespace vje
{
	//=================================================================================================================
	// File-Local Helpers
	//=================================================================================================================

	namespace
	{
		// ASCII digits only, deliberately: QChar::isDigit() is true for every Unicode decimal digit, and an Arabic-Indic
		// five in an array index would be read as a number by the scanner and rejected by nothing.

		bool is_ascii_digit ( QChar character )
		{
			return ( character >= QLatin1Char ( '0' ) ) && ( character <= QLatin1Char ( '9' ) );
		}

		// What a BARE (unbracketed) member name may contain. Letters, digits, underscore and hyphen -- the last because
		// hyphenated keys are ordinary in JSON and requiring brackets for them would make the common case the awkward
		// one. Anything else (a space, a dot, a quote) needs the bracketed form, which is what it is for.

		bool is_bare_name_character ( QChar character )
		{
			return character.isLetter ()
			    || is_ascii_digit ( character )
			    || ( character == QLatin1Char ( '_' ) )
			    || ( character == QLatin1Char ( '-' ) );
		}

		// Remove repeats, keeping the FIRST occurrence of each node. Applied to the working set after every segment.
		//
		// What it bounds is the WORK, not the answer: evaluation ends in a set, so the result is duplicate-free either
		// way. Without it the intermediate set of "$..*..*" grows multiplicatively over a document that has no more
		// nodes than it started with, which is a cost with no upper bound in the document's size.

		void remove_duplicate_nodes ( std::vector<const JsonNode*>& nodes )
		{
			std::unordered_set<const JsonNode*> seen;
			std::vector<const JsonNode*>        unique;

			unique.reserve ( nodes.size () );

			for ( const JsonNode* const node : nodes )
			{
				if ( seen.insert ( node ).second )
				{
					unique.push_back ( node );
				}
			}

			nodes.swap ( unique );
		}

		// One pre-order walk of the tree, emitting the pointer of every node in the selected set. This is what makes the
		// result order the DOCUMENT's rather than the order the selectors happened to visit things in (QUERY-06).

		void emit_pointers
		(
			const JsonNode&                           node,
			const JsonPointer&                        pointer,
			const std::unordered_set<const JsonNode*>& selected,
			std::vector<JsonPointer>&                 out
		)
		{
			if ( selected.find ( &node ) != selected.end () )
			{
				out.push_back ( pointer );
			}

			if ( node.kind () == JsonKind::Array )
			{
				const int elementCount = node.array_size ();

				for ( int index = 0; index < elementCount; ++index )
				{
					const JsonNode* const element = node.array_element ( index );

					if ( element != nullptr )
					{
						emit_pointers ( *element, pointer.child ( QString::number ( index ) ), selected, out );
					}
				}
			}
			else if ( node.kind () == JsonKind::Object )
			{
				const int memberCount = node.member_count ();

				for ( int index = 0; index < memberCount; ++index )
				{
					const JsonNode* const member = node.member_value ( index );

					if ( member != nullptr )
					{
						emit_pointers ( *member, pointer.child ( node.member_key ( index ) ), selected, out );
					}
				}
			}
		}
	}

	//*****************************************************************************************************************
	// Struct: JsonPathQuery::Parser
	//
	// The whole grammar of spec section 2.7, as recursive descent over the query text.
	//*****************************************************************************************************************

	struct JsonPathQuery::Parser
	{
		//=============================================================================================================
		// Data Members
		//=============================================================================================================

		const QString& source;
		int            position = 0;

		JsonPathError error;
		bool          failed = false;

		std::vector<Segment>&    segments;
		std::vector<Expression>& expressions;

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

		Parser ( const QString& text, std::vector<Segment>& outSegments, std::vector<Expression>& outExpressions )
		:
			source      ( text ),
			segments    ( outSegments ),
			expressions ( outExpressions )
		{
		}

		//=============================================================================================================
		// Scanning
		//=============================================================================================================

		bool at_end () const
		{
			return position >= static_cast<int> ( source.size () );
		}

		QChar peek ( int offset = 0 ) const
		{
			const int index = position + offset;

			return ( index < static_cast<int> ( source.size () ) ) ? source.at ( index ) : QChar ();
		}

		void skip_whitespace ()
		{
			while ( !at_end () && source.at ( position ).isSpace () )
			{
				++position;
			}
		}

		// Record the FIRST failure and nothing after it -- see the file header.

		bool fail ( const QString& message, int at, int length )
		{
			if ( !failed )
			{
				failed         = true;
				error.message  = message;
				error.position = at;
				error.length   = length;
			}

			return false;
		}

		// The span to mark when the offending thing is a single character, or the end of the input (where there is
		// nothing to mark and the caret goes to the end).

		int span_here () const
		{
			return at_end () ? 0 : 1;
		}

		// A keyword, matched only when it is not the prefix of a longer bare name -- so "nullable" is a name and not
		// null followed by rubbish.

		bool matches_keyword ( QLatin1String keyword ) const
		{
			const int length = keyword.size ();

			if ( ( position + length ) > static_cast<int> ( source.size () ) )
			{
				return false;
			}

			if ( source.mid ( position, length ) != keyword )
			{
				return false;
			}

			const QChar following = peek ( length );

			return following.isNull () || !is_bare_name_character ( following );
		}

		//=============================================================================================================
		// The Path
		//=============================================================================================================

		bool parse ()
		{
			skip_whitespace ();

			if ( peek () != QLatin1Char ( '$' ) )
			{
				return fail ( QStringLiteral ( "A query must begin with '$'." ), position, span_here () );
			}

			++position;

			while ( true )
			{
				skip_whitespace ();

				if ( at_end () )
				{
					return true;
				}

				if ( !parse_segment () )
				{
					return false;
				}
			}
		}

		bool parse_segment ()
		{
			Segment segment;

			if ( ( peek () == QLatin1Char ( '.' ) ) && ( peek ( 1 ) == QLatin1Char ( '.' ) ) )
			{
				segment.descendant = true;

				position += 2;

				skip_whitespace ();

				if ( at_end () )
				{
					return fail ( QStringLiteral ( "'..' must be followed by a name, '*' or a bracketed selector." ), position, 0 );
				}

				if ( peek () == QLatin1Char ( '[' ) )
				{
					if ( !parse_bracket_selectors ( segment ) )
					{
						return false;
					}
				}
				else if ( peek () == QLatin1Char ( '*' ) )
				{
					++position;

					segment.selectors.push_back ( wildcard_selector () );
				}
				else if ( !parse_bare_name_selector ( segment ) )
				{
					return false;
				}
			}
			else if ( peek () == QLatin1Char ( '.' ) )
			{
				++position;

				skip_whitespace ();

				if ( peek () == QLatin1Char ( '*' ) )
				{
					++position;

					segment.selectors.push_back ( wildcard_selector () );
				}
				else if ( peek () == QLatin1Char ( '[' ) )
				{
					return fail ( QStringLiteral ( "Write '[' on its own or '.name'; '.[' is not accepted." ), position - 1, 2 );
				}
				else if ( !parse_bare_name_selector ( segment ) )
				{
					return false;
				}
			}
			else if ( peek () == QLatin1Char ( '[' ) )
			{
				if ( !parse_bracket_selectors ( segment ) )
				{
					return false;
				}
			}
			else if ( peek () == QLatin1Char ( '^' ) )
			{
				return fail ( QStringLiteral ( "The parent operator '^' is not supported." ), position, 1 );
			}
			else
			{
				return fail ( QStringLiteral ( "Expected '.', '..' or '['." ), position, span_here () );
			}

			segments.push_back ( segment );

			return true;
		}

		Selector wildcard_selector () const
		{
			Selector selector;

			selector.kind = SelectorKind::Wildcard;

			return selector;
		}

		bool parse_bare_name_selector ( Segment& segment )
		{
			const int start = position;

			while ( !at_end () && is_bare_name_character ( source.at ( position ) ) )
			{
				++position;
			}

			if ( position == start )
			{
				return fail ( QStringLiteral ( "Expected a member name." ), start, span_here () );
			}

			Selector selector;

			selector.kind = SelectorKind::Name;
			selector.name = source.mid ( start, position - start );

			segment.selectors.push_back ( selector );

			return true;
		}

		//=============================================================================================================
		// Bracketed Selectors, and the Union
		//=============================================================================================================

		bool parse_bracket_selectors ( Segment& segment )
		{
			const int bracketStart = position;

			++position;

			while ( true )
			{
				skip_whitespace ();

				if ( at_end () )
				{
					return fail ( QStringLiteral ( "Unterminated '['." ), bracketStart, 1 );
				}

				Selector selector;

				if ( !parse_selector ( selector ) )
				{
					return false;
				}

				segment.selectors.push_back ( selector );

				skip_whitespace ();

				if ( peek () == QLatin1Char ( ',' ) )
				{
					++position;

					continue;
				}

				break;
			}

			if ( peek () != QLatin1Char ( ']' ) )
			{
				return fail ( QStringLiteral ( "Expected ']'." ), position, span_here () );
			}

			++position;

			// A union is several selectors at one step, and it is bounded to names and indices. A wildcard, a slice or
			// a filter beside them would each mean "and also everything matching this", which is a second question in
			// the same brackets -- and every one of them is expressible as its own step instead.

			if ( segment.selectors.size () > 1 )
			{
				for ( const Selector& selector : segment.selectors )
				{
					if ( ( selector.kind != SelectorKind::Name ) && ( selector.kind != SelectorKind::Index ) )
					{
						return fail
						(
							QStringLiteral ( "A union may contain only names and indices." ),
							bracketStart,
							position - bracketStart
						);
					}
				}
			}

			return true;
		}

		bool parse_selector ( Selector& selector )
		{
			const QChar character = source.at ( position );

			if ( character == QLatin1Char ( '*' ) )
			{
				++position;

				selector.kind = SelectorKind::Wildcard;

				return true;
			}

			if ( character == QLatin1Char ( '?' ) )
			{
				return parse_filter ( selector );
			}

			if ( ( character == QLatin1Char ( '\'' ) ) || ( character == QLatin1Char ( '"' ) ) )
			{
				QString name;

				if ( !parse_string_literal ( name ) )
				{
					return false;
				}

				selector.kind = SelectorKind::Name;
				selector.name = name;

				return true;
			}

			if ( character == QLatin1Char ( '(' ) )
			{
				return fail ( QStringLiteral ( "Script expressions are not supported." ), position, 1 );
			}

			return parse_slice_or_index ( selector );
		}

		// An optional signed integer. Consumes nothing at all when there is no number, so a caller can tell "absent"
		// (a slice bound) from "malformed" without having to put anything back.

		bool parse_optional_integer ( int& value )
		{
			const int start     = position;
			bool      negative  = false;
			int       cursor    = position;

			if ( ( cursor < static_cast<int> ( source.size () ) ) && ( source.at ( cursor ) == QLatin1Char ( '-' ) ) )
			{
				negative = true;

				++cursor;
			}

			int digitCount = 0;

			qlonglong magnitude = 0;

			while ( ( cursor < static_cast<int> ( source.size () ) ) && is_ascii_digit ( source.at ( cursor ) ) )
			{
				if ( magnitude <= 0x7FFFFFFF )
				{
					magnitude = ( magnitude * 10 ) + ( source.at ( cursor ).unicode () - u'0' );
				}

				++digitCount;
				++cursor;
			}

			if ( digitCount == 0 )
			{
				return false;
			}

			position = cursor;

			if ( magnitude > 0x7FFFFFFF )
			{
				fail ( QStringLiteral ( "The number is too large." ), start, position - start );

				return false;
			}

			value = negative ? -static_cast<int> ( magnitude ) : static_cast<int> ( magnitude );

			return true;
		}

		bool parse_slice_or_index ( Selector& selector )
		{
			const int start = position;

			int        firstValue = 0;
			const bool hasFirst   = parse_optional_integer ( firstValue );

			if ( failed )
			{
				return false;
			}

			skip_whitespace ();

			if ( peek () == QLatin1Char ( ':' ) )
			{
				selector.kind     = SelectorKind::Slice;
				selector.hasStart = hasFirst;
				selector.start    = firstValue;

				++position;

				skip_whitespace ();

				int        endValue = 0;
				const bool hasEnd   = parse_optional_integer ( endValue );

				if ( failed )
				{
					return false;
				}

				selector.hasEnd = hasEnd;
				selector.end    = endValue;

				skip_whitespace ();

				if ( peek () == QLatin1Char ( ':' ) )
				{
					++position;

					skip_whitespace ();

					const int stepStart = position;

					int        stepValue = 0;
					const bool hasStep   = parse_optional_integer ( stepValue );

					if ( failed )
					{
						return false;
					}

					if ( hasStep )
					{
						// A non-positive step is refused rather than reinterpreted: a zero step does not terminate, and
						// a negative one asks for a reversed result set, which QUERY-06's document order would then
						// silently put back the way it was.

						if ( stepValue <= 0 )
						{
							return fail
							(
								QStringLiteral ( "A slice step must be a positive integer." ),
								stepStart,
								position - stepStart
							);
						}

						selector.step = stepValue;
					}
				}

				return true;
			}

			if ( !hasFirst )
			{
				return fail ( QStringLiteral ( "Expected an index, a name, '*' or a filter." ), start, span_here () );
			}

			selector.kind  = SelectorKind::Index;
			selector.index = firstValue;

			return true;
		}

		//=============================================================================================================
		// String Literals
		//
		// The body is gathered with a parallel table of SOURCE offsets and then decoded by json_escapes -- the
		// codebase's one escape table (json_escapes.hpp) -- so a malformed escape is reported at the backslash that
		// caused it rather than at the start of the literal. The one addition to the JSON set is \' , which a
		// single-quoted literal needs and which is stripped here before the table ever sees it.
		//=============================================================================================================

		bool parse_string_literal ( QString& out )
		{
			const QChar quote        = source.at ( position );
			const int   literalStart = position;

			++position;

			QString          body;
			std::vector<int> sourceOffsets;

			while ( true )
			{
				if ( at_end () )
				{
					return fail ( QStringLiteral ( "Unterminated string." ), literalStart, position - literalStart );
				}

				const QChar character = source.at ( position );

				if ( character == quote )
				{
					++position;

					break;
				}

				if ( character == QLatin1Char ( '\\' ) )
				{
					if ( ( position + 1 ) >= static_cast<int> ( source.size () ) )
					{
						return fail ( QStringLiteral ( "Unterminated string." ), literalStart, position - literalStart );
					}

					const QChar escaped = source.at ( position + 1 );

					if ( escaped == QLatin1Char ( '\'' ) )
					{
						body.append ( escaped );
						sourceOffsets.push_back ( position );

						position += 2;

						continue;
					}

					body.append ( character );
					sourceOffsets.push_back ( position );

					body.append ( escaped );
					sourceOffsets.push_back ( position + 1 );

					position += 2;

					continue;
				}

				body.append ( character );
				sourceOffsets.push_back ( position );

				++position;
			}

			QString value;
			int     errorOffset = 0;

			if ( !json_escapes::decode ( body, value, &errorOffset ) )
			{
				const bool known = ( errorOffset >= 0 ) && ( errorOffset < static_cast<int> ( sourceOffsets.size () ) );

				return fail
				(
					QStringLiteral ( "Malformed escape in a string literal." ),
					known ? sourceOffsets [ static_cast<size_t> ( errorOffset ) ] : literalStart,
					1
				);
			}

			out = value;

			return true;
		}

		//=============================================================================================================
		// Filter Expressions (QUERY-03)
		//=============================================================================================================

		bool parse_filter ( Selector& selector )
		{
			const int filterStart = position;

			++position;

			skip_whitespace ();

			if ( peek () != QLatin1Char ( '(' ) )
			{
				return fail ( QStringLiteral ( "A filter must be written [?( ... )]." ), filterStart, 1 );
			}

			++position;

			int root = -1;

			if ( !parse_or_expression ( root ) )
			{
				return false;
			}

			skip_whitespace ();

			if ( peek () != QLatin1Char ( ')' ) )
			{
				return fail ( QStringLiteral ( "Expected ')' to close the filter." ), position, span_here () );
			}

			++position;

			selector.kind       = SelectorKind::Filter;
			selector.filterRoot = root;

			return true;
		}

		int push_expression ( const Expression& expression )
		{
			expressions.push_back ( expression );

			return static_cast<int> ( expressions.size () ) - 1;
		}

		bool parse_or_expression ( int& out )
		{
			int left = -1;

			if ( !parse_and_expression ( left ) )
			{
				return false;
			}

			while ( true )
			{
				skip_whitespace ();

				if ( ( peek () == QLatin1Char ( '|' ) ) && ( peek ( 1 ) == QLatin1Char ( '|' ) ) )
				{
					position += 2;

					int right = -1;

					if ( !parse_and_expression ( right ) )
					{
						return false;
					}

					Expression node;

					node.kind  = ExpressionKind::Or;
					node.left  = left;
					node.right = right;

					left = push_expression ( node );

					continue;
				}

				if ( peek () == QLatin1Char ( '|' ) )
				{
					return fail ( QStringLiteral ( "Expected '||'." ), position, 1 );
				}

				break;
			}

			out = left;

			return true;
		}

		bool parse_and_expression ( int& out )
		{
			int left = -1;

			if ( !parse_unary_expression ( left ) )
			{
				return false;
			}

			while ( true )
			{
				skip_whitespace ();

				if ( ( peek () == QLatin1Char ( '&' ) ) && ( peek ( 1 ) == QLatin1Char ( '&' ) ) )
				{
					position += 2;

					int right = -1;

					if ( !parse_unary_expression ( right ) )
					{
						return false;
					}

					Expression node;

					node.kind  = ExpressionKind::And;
					node.left  = left;
					node.right = right;

					left = push_expression ( node );

					continue;
				}

				if ( peek () == QLatin1Char ( '&' ) )
				{
					return fail ( QStringLiteral ( "Expected '&&'." ), position, 1 );
				}

				break;
			}

			out = left;

			return true;
		}

		bool parse_unary_expression ( int& out )
		{
			skip_whitespace ();

			if ( at_end () )
			{
				return fail ( QStringLiteral ( "Expected a filter expression." ), position, 0 );
			}

			if ( ( peek () == QLatin1Char ( '!' ) ) && ( peek ( 1 ) != QLatin1Char ( '=' ) ) )
			{
				++position;

				int inner = -1;

				if ( !parse_unary_expression ( inner ) )
				{
					return false;
				}

				Expression node;

				node.kind = ExpressionKind::Not;
				node.left = inner;

				out = push_expression ( node );

				return true;
			}

			if ( peek () == QLatin1Char ( '(' ) )
			{
				++position;

				int inner = -1;

				if ( !parse_or_expression ( inner ) )
				{
					return false;
				}

				skip_whitespace ();

				if ( peek () != QLatin1Char ( ')' ) )
				{
					return fail ( QStringLiteral ( "Expected ')'." ), position, span_here () );
				}

				++position;

				out = inner;

				return true;
			}

			return parse_comparison_expression ( out );
		}

		bool match_comparison_operator ( ComparisonOperator& comparison )
		{
			struct OperatorSpelling
			{
				QLatin1String      text;
				ComparisonOperator comparison;
			};

			// Two characters before one, so "<=" is never read as "<" followed by rubbish.

			static const OperatorSpelling SPELLINGS [] =
			{
				{ QLatin1String ( "==" ), ComparisonOperator::Equal          },
				{ QLatin1String ( "!=" ), ComparisonOperator::NotEqual       },
				{ QLatin1String ( "<=" ), ComparisonOperator::LessOrEqual    },
				{ QLatin1String ( ">=" ), ComparisonOperator::GreaterOrEqual },
				{ QLatin1String ( "<"  ), ComparisonOperator::Less           },
				{ QLatin1String ( ">"  ), ComparisonOperator::Greater        }
			};

			for ( const OperatorSpelling& spelling : SPELLINGS )
			{
				const int length = spelling.text.size ();

				if ( ( position + length ) > static_cast<int> ( source.size () ) )
				{
					continue;
				}

				if ( source.mid ( position, length ) == spelling.text )
				{
					position   += length;
					comparison  = spelling.comparison;

					return true;
				}
			}

			return false;
		}

		bool parse_comparison_expression ( int& out )
		{
			Operand left;

			if ( !parse_operand ( left ) )
			{
				return false;
			}

			skip_whitespace ();

			const int operatorStart = position;

			ComparisonOperator comparison = ComparisonOperator::Equal;

			if ( match_comparison_operator ( comparison ) )
			{
				Operand right;

				if ( !parse_operand ( right ) )
				{
					return false;
				}

				Expression node;

				node.kind         = ExpressionKind::Comparison;
				node.comparison   = comparison;
				node.leftOperand  = left;
				node.rightOperand = right;

				out = push_expression ( node );

				return true;
			}

			// The three things a user most plausibly writes next, each named rather than swept into "Expected ')'".

			if ( ( peek () == QLatin1Char ( '=' ) ) && ( peek ( 1 ) == QLatin1Char ( '~' ) ) )
			{
				return fail ( QStringLiteral ( "Regular-expression matching is not supported." ), operatorStart, 2 );
			}

			if ( peek () == QLatin1Char ( '=' ) )
			{
				return fail ( QStringLiteral ( "Expected '=='." ), operatorStart, 1 );
			}

			if ( is_arithmetic_operator ( peek () ) )
			{
				return fail ( QStringLiteral ( "Arithmetic is not supported inside a filter." ), operatorStart, 1 );
			}

			Expression node;

			node.kind        = ExpressionKind::Existence;
			node.leftOperand = left;

			out = push_expression ( node );

			return true;
		}

		static bool is_arithmetic_operator ( QChar character )
		{
			return ( character == QLatin1Char ( '+' ) )
			    || ( character == QLatin1Char ( '-' ) )
			    || ( character == QLatin1Char ( '*' ) )
			    || ( character == QLatin1Char ( '/' ) )
			    || ( character == QLatin1Char ( '%' ) );
		}

		bool parse_operand ( Operand& operand )
		{
			skip_whitespace ();

			if ( at_end () )
			{
				return fail ( QStringLiteral ( "Expected an operand." ), position, 0 );
			}

			const QChar character = source.at ( position );

			if ( character == QLatin1Char ( '@' ) )
			{
				return parse_relative_path ( operand );
			}

			if ( ( character == QLatin1Char ( '\'' ) ) || ( character == QLatin1Char ( '"' ) ) )
			{
				QString value;

				if ( !parse_string_literal ( value ) )
				{
					return false;
				}

				operand.kind   = OperandKind::String;
				operand.string = value;

				return true;
			}

			if ( is_ascii_digit ( character ) || ( character == QLatin1Char ( '-' ) ) )
			{
				return parse_number_literal ( operand );
			}

			if ( matches_keyword ( QLatin1String ( "true" ) ) )
			{
				position += 4;

				operand.kind    = OperandKind::Boolean;
				operand.boolean = true;

				return true;
			}

			if ( matches_keyword ( QLatin1String ( "false" ) ) )
			{
				position += 5;

				operand.kind    = OperandKind::Boolean;
				operand.boolean = false;

				return true;
			}

			if ( matches_keyword ( QLatin1String ( "null" ) ) )
			{
				position += 4;

				operand.kind = OperandKind::Null;

				return true;
			}

			if ( character == QLatin1Char ( '$' ) )
			{
				return fail
				(
					QStringLiteral ( "A filter's path starts at '@'; '$' is not supported inside a filter." ),
					position,
					1
				);
			}

			// A bare word here is almost always a function call, which is worth saying rather than leaving as
			// "Expected an operand" beside a perfectly well-formed name.

			if ( character.isLetter () || ( character == QLatin1Char ( '_' ) ) )
			{
				const int start = position;

				while ( !at_end () && is_bare_name_character ( source.at ( position ) ) )
				{
					++position;
				}

				const int wordEnd = position;

				skip_whitespace ();

				if ( peek () == QLatin1Char ( '(' ) )
				{
					return fail ( QStringLiteral ( "Functions are not supported inside a filter." ), start, wordEnd - start );
				}

				return fail ( QStringLiteral ( "Expected an operand." ), start, wordEnd - start );
			}

			return fail ( QStringLiteral ( "Expected an operand." ), position, 1 );
		}

		bool parse_number_literal ( Operand& operand )
		{
			const int start = position;

			if ( peek () == QLatin1Char ( '-' ) )
			{
				++position;
			}

			int integerDigits = 0;

			while ( !at_end () && is_ascii_digit ( source.at ( position ) ) )
			{
				++position;
				++integerDigits;
			}

			if ( integerDigits == 0 )
			{
				return fail ( QStringLiteral ( "Expected a number." ), start, span_here () );
			}

			if ( peek () == QLatin1Char ( '.' ) )
			{
				++position;

				int fractionDigits = 0;

				while ( !at_end () && is_ascii_digit ( source.at ( position ) ) )
				{
					++position;
					++fractionDigits;
				}

				if ( fractionDigits == 0 )
				{
					return fail ( QStringLiteral ( "Expected a digit after '.'." ), position, span_here () );
				}
			}

			if ( ( peek () == QLatin1Char ( 'e' ) ) || ( peek () == QLatin1Char ( 'E' ) ) )
			{
				++position;

				if ( ( peek () == QLatin1Char ( '+' ) ) || ( peek () == QLatin1Char ( '-' ) ) )
				{
					++position;
				}

				int exponentDigits = 0;

				while ( !at_end () && is_ascii_digit ( source.at ( position ) ) )
				{
					++position;
					++exponentDigits;
				}

				if ( exponentDigits == 0 )
				{
					return fail ( QStringLiteral ( "Expected a digit in the exponent." ), position, span_here () );
				}
			}

			bool converted = false;

			const double value = source.mid ( start, position - start ).toDouble ( &converted );

			if ( !converted )
			{
				return fail ( QStringLiteral ( "The number could not be read." ), start, position - start );
			}

			operand.kind   = OperandKind::Number;
			operand.number = value;

			return true;
		}

		// "@" and what may follow it. Every construct QUERY-04 excludes from a filter's path is refused BY NAME here
		// rather than by falling off the end of the loop, because the difference between "not supported" and "I stopped
		// reading here" is the difference between a user correcting the query and a user guessing at it.

		bool parse_relative_path ( Operand& operand )
		{
			++position;

			operand.kind = OperandKind::RelativePath;

			while ( true )
			{
				skip_whitespace ();

				if ( peek () == QLatin1Char ( '.' ) )
				{
					if ( peek ( 1 ) == QLatin1Char ( '.' ) )
					{
						return fail ( QStringLiteral ( "Recursive descent is not supported inside a filter." ), position, 2 );
					}

					++position;

					if ( peek () == QLatin1Char ( '*' ) )
					{
						return fail ( QStringLiteral ( "A wildcard is not supported inside a filter." ), position, 1 );
					}

					const int start = position;

					while ( !at_end () && is_bare_name_character ( source.at ( position ) ) )
					{
						++position;
					}

					if ( position == start )
					{
						return fail ( QStringLiteral ( "Expected a member name." ), start, span_here () );
					}

					PathStep step;

					step.name = source.mid ( start, position - start );

					operand.pathSteps.push_back ( step );

					continue;
				}

				if ( peek () == QLatin1Char ( '[' ) )
				{
					if ( !parse_relative_path_bracket ( operand ) )
					{
						return false;
					}

					continue;
				}

				break;
			}

			return true;
		}

		bool parse_relative_path_bracket ( Operand& operand )
		{
			const int bracketStart = position;

			++position;

			skip_whitespace ();

			if ( at_end () )
			{
				return fail ( QStringLiteral ( "Unterminated '['." ), bracketStart, 1 );
			}

			const QChar character = source.at ( position );

			if ( ( character == QLatin1Char ( '\'' ) ) || ( character == QLatin1Char ( '"' ) ) )
			{
				QString name;

				if ( !parse_string_literal ( name ) )
				{
					return false;
				}

				PathStep step;

				step.name = name;

				operand.pathSteps.push_back ( step );
			}
			else if ( character == QLatin1Char ( '?' ) )
			{
				return fail ( QStringLiteral ( "A filter is not supported inside a filter's path." ), position, 1 );
			}
			else if ( character == QLatin1Char ( '*' ) )
			{
				return fail ( QStringLiteral ( "A wildcard is not supported inside a filter." ), position, 1 );
			}
			else
			{
				const int numberStart = position;

				int value = 0;

				if ( !parse_optional_integer ( value ) )
				{
					if ( failed )
					{
						return false;
					}

					return fail ( QStringLiteral ( "Expected an array index." ), numberStart, span_here () );
				}

				if ( value < 0 )
				{
					return fail
					(
						QStringLiteral ( "A negative index is not supported inside a filter." ),
						numberStart,
						position - numberStart
					);
				}

				skip_whitespace ();

				if ( peek () == QLatin1Char ( ':' ) )
				{
					return fail ( QStringLiteral ( "A slice is not supported inside a filter." ), position, 1 );
				}

				PathStep step;

				step.isIndex = true;
				step.index   = value;

				operand.pathSteps.push_back ( step );
			}

			skip_whitespace ();

			if ( peek () != QLatin1Char ( ']' ) )
			{
				return fail ( QStringLiteral ( "Expected ']'." ), position, span_here () );
			}

			++position;

			return true;
		}
	};

	//=================================================================================================================
	// Factories
	//=================================================================================================================

	JsonPathQuery JsonPathQuery::compile ( const QString& text )
	{
		JsonPathQuery query;

		query.queryText = text;

		// An empty query is INVALID with no message. It is not a mistake -- there is nothing to report (QUERY-05) --
		// and giving it a third state would put the distinction in every caller instead of in one field.

		if ( text.trimmed ().isEmpty () )
		{
			return query;
		}

		Parser parser ( text, query.segments, query.expressions );

		if ( parser.parse () )
		{
			query.valid = true;
		}
		else
		{
			query.compileError = parser.error;

			query.segments.clear ();
			query.expressions.clear ();
		}

		return query;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool JsonPathQuery::is_valid () const
	{
		return valid;
	}

	const JsonPathError& JsonPathQuery::error () const
	{
		return compileError;
	}

	const QString& JsonPathQuery::text () const
	{
		return queryText;
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	std::vector<JsonPointer> JsonPathQuery::evaluate ( const JsonNode& root ) const
	{
		std::vector<JsonPointer> results;

		if ( !valid )
		{
			return results;
		}

		std::vector<const JsonNode*> nodes;

		nodes.push_back ( &root );

		for ( const Segment& segment : segments )
		{
			apply_segment ( segment, nodes );

			if ( nodes.empty () )
			{
				return results;
			}
		}

		const std::unordered_set<const JsonNode*> selected ( nodes.begin (), nodes.end () );

		emit_pointers ( root, JsonPointer (), selected, results );

		return results;
	}

	std::vector<JsonPointer> JsonPathQuery::run ( const JsonNode& root, const QString& text, JsonPathError* outError )
	{
		const JsonPathQuery query = compile ( text );

		if ( outError != nullptr )
		{
			*outError = query.is_valid () ? JsonPathError () : query.error ();
		}

		return query.evaluate ( root );
	}

	//=================================================================================================================
	// Internals -- filter values (QUERY-03)
	//=================================================================================================================

	JsonPathQuery::FilterValue JsonPathQuery::operand_value ( const Operand& operand, const JsonNode& node )
	{
		FilterValue value;

		switch ( operand.kind )
		{
			case OperandKind::Number:
			{
				value.kind   = ValueKind::Number;
				value.number = operand.number;

				break;
			}

			case OperandKind::String:
			{
				value.kind   = ValueKind::String;
				value.string = operand.string;

				break;
			}

			case OperandKind::Boolean:
			{
				value.kind    = ValueKind::Boolean;
				value.boolean = operand.boolean;

				break;
			}

			case OperandKind::Null:
			{
				value.kind = ValueKind::Null;

				break;
			}

			case OperandKind::RelativePath:
			{
				const JsonNode* const resolved = resolve_relative_path ( operand.pathSteps, node );

				if ( resolved == nullptr )
				{
					value.kind = ValueKind::Absent;

					break;
				}

				switch ( resolved->kind () )
				{
					case JsonKind::Null:    value.kind = ValueKind::Null; break;

					case JsonKind::Boolean:
					{
						value.kind    = ValueKind::Boolean;
						value.boolean = resolved->boolean_value ();

						break;
					}

					case JsonKind::Number:
					{
						// By VALUE, not by token. FILE-10's exact lexeme is what a save writes; it is not what an
						// inequality asks about (QUERY-03).

						value.kind   = ValueKind::Number;
						value.number = resolved->number_token ().toDouble ();

						break;
					}

					case JsonKind::String:
					{
						value.kind   = ValueKind::String;
						value.string = resolved->string_value ();

						break;
					}

					case JsonKind::Array:
					case JsonKind::Object:
					{
						value.kind = ValueKind::Container;

						break;
					}
				}

				break;
			}
		}

		return value;
	}

	bool JsonPathQuery::compare_values ( const FilterValue& left, ComparisonOperator comparison, const FilterValue& right )
	{
		// An operand that names nothing makes EVERY comparison false, "!=" included. The alternative reading -- that a
		// missing member differs from 10 and so satisfies "@.price != 10" -- makes a filter select the elements it knows
		// least about, which is never what was meant (QUERY-03).

		if ( ( left.kind == ValueKind::Absent ) || ( right.kind == ValueKind::Absent ) )
		{
			return false;
		}

		// A container is PRESENT (so it satisfies an existence test) and compares equal to nothing, itself included:
		// there is no equality on subtrees here that would not immediately raise the question of which one.

		if ( ( left.kind == ValueKind::Container ) || ( right.kind == ValueKind::Container ) )
		{
			return comparison == ComparisonOperator::NotEqual;
		}

		if ( left.kind != right.kind )
		{
			return comparison == ComparisonOperator::NotEqual;
		}

		int ordering = 0;

		switch ( left.kind )
		{
			case ValueKind::Null:
			{
				// Two nulls are equal and have no order, so an ordering comparison on them is false rather than true.

				if ( comparison == ComparisonOperator::Equal    ) return true;
				if ( comparison == ComparisonOperator::NotEqual ) return false;

				return false;
			}

			case ValueKind::Boolean:
			{
				if ( comparison == ComparisonOperator::Equal    ) return left.boolean == right.boolean;
				if ( comparison == ComparisonOperator::NotEqual ) return left.boolean != right.boolean;

				return false;
			}

			case ValueKind::Number:
			{
				if ( left.number < right.number ) ordering = -1;
				if ( left.number > right.number ) ordering =  1;

				break;
			}

			case ValueKind::String:
			{
				ordering = QString::compare ( left.string, right.string );

				break;
			}

			case ValueKind::Absent:
			case ValueKind::Container:
			{
				return false;
			}
		}

		switch ( comparison )
		{
			case ComparisonOperator::Equal:          return ordering == 0;
			case ComparisonOperator::NotEqual:       return ordering != 0;
			case ComparisonOperator::Less:           return ordering <  0;
			case ComparisonOperator::LessOrEqual:    return ordering <= 0;
			case ComparisonOperator::Greater:        return ordering >  0;
			case ComparisonOperator::GreaterOrEqual: return ordering >= 0;
		}

		return false;
	}

	bool JsonPathQuery::value_is_truthy ( const FilterValue& value )
	{
		if ( value.kind == ValueKind::Absent )  return false;
		if ( value.kind == ValueKind::Null )    return false;
		if ( value.kind == ValueKind::Boolean ) return value.boolean;

		return true;
	}

	//=================================================================================================================
	// Internals -- evaluation
	//=================================================================================================================

	void JsonPathQuery::apply_segment ( const Segment& segment, std::vector<const JsonNode*>& nodes ) const
	{
		std::vector<const JsonNode*> candidates;

		if ( segment.descendant )
		{
			// ".." selects from the node ITSELF as well as from everything under it, which is what makes "$..name" find
			// a top-level "name" as well as a nested one.

			for ( const JsonNode* const node : nodes )
			{
				collect_self_and_descendants ( *node, candidates );
			}

			remove_duplicate_nodes ( candidates );
		}
		else
		{
			candidates.swap ( nodes );
		}

		std::vector<const JsonNode*> selected;

		for ( const JsonNode* const node : candidates )
		{
			for ( const Selector& selector : segment.selectors )
			{
				apply_selector ( selector, *node, selected );
			}
		}

		remove_duplicate_nodes ( selected );

		nodes.swap ( selected );
	}

	void JsonPathQuery::apply_selector ( const Selector& selector, const JsonNode& node, std::vector<const JsonNode*>& out ) const
	{
		switch ( selector.kind )
		{
			case SelectorKind::Name:
			{
				if ( node.kind () == JsonKind::Object )
				{
					// The FIRST member of that key when the object carries duplicates -- what a JSON Pointer means
					// everywhere else in VJE (see the header).

					const JsonNode* const member = node.find_member ( selector.name );

					if ( member != nullptr )
					{
						out.push_back ( member );
					}
				}

				break;
			}

			case SelectorKind::Index:
			{
				if ( node.kind () == JsonKind::Array )
				{
					const int elementCount = node.array_size ();
					const int index        = ( selector.index < 0 ) ? ( elementCount + selector.index ) : selector.index;

					const JsonNode* const element = node.array_element ( index );

					if ( element != nullptr )
					{
						out.push_back ( element );
					}
				}

				break;
			}

			case SelectorKind::Slice:
			{
				if ( node.kind () == JsonKind::Array )
				{
					const int elementCount = node.array_size ();

					int start = selector.hasStart ? selector.start : 0;
					int end   = selector.hasEnd   ? selector.end   : elementCount;

					if ( start < 0 ) start += elementCount;
					if ( end   < 0 ) end   += elementCount;

					// The clamp bounds the LOOP, not the result: an out-of-range index is nullptr and skipped below,
					// so the answer is the same without it. What is not the same is the cost -- "[-2000000000:]"
					// otherwise walks two billion indices to produce two elements, which is what
					// a_slice_bound_is_clamped_rather_than_merely_skipped measures.

					start = qBound ( 0, start, elementCount );
					end   = qBound ( 0, end,   elementCount );

					for ( int index = start; index < end; index += selector.step )
					{
						const JsonNode* const element = node.array_element ( index );

						if ( element != nullptr )
						{
							out.push_back ( element );
						}
					}
				}

				break;
			}

			case SelectorKind::Wildcard:
			{
				if ( node.kind () == JsonKind::Array )
				{
					const int elementCount = node.array_size ();

					for ( int index = 0; index < elementCount; ++index )
					{
						const JsonNode* const element = node.array_element ( index );

						if ( element != nullptr )
						{
							out.push_back ( element );
						}
					}
				}
				else if ( node.kind () == JsonKind::Object )
				{
					const int memberCount = node.member_count ();

					for ( int index = 0; index < memberCount; ++index )
					{
						const JsonNode* const member = node.member_value ( index );

						if ( member != nullptr )
						{
							out.push_back ( member );
						}
					}
				}

				break;
			}

			case SelectorKind::Filter:
			{
				// A filter selects among the CHILDREN of the node it is applied to -- "$.projects[?(...)]" tests each
				// element of projects, not projects itself.

				if ( node.kind () == JsonKind::Array )
				{
					const int elementCount = node.array_size ();

					for ( int index = 0; index < elementCount; ++index )
					{
						const JsonNode* const element = node.array_element ( index );

						if ( ( element != nullptr ) && test_expression ( selector.filterRoot, *element ) )
						{
							out.push_back ( element );
						}
					}
				}
				else if ( node.kind () == JsonKind::Object )
				{
					const int memberCount = node.member_count ();

					for ( int index = 0; index < memberCount; ++index )
					{
						const JsonNode* const member = node.member_value ( index );

						if ( ( member != nullptr ) && test_expression ( selector.filterRoot, *member ) )
						{
							out.push_back ( member );
						}
					}
				}

				break;
			}
		}
	}

	bool JsonPathQuery::test_expression ( int expressionIndex, const JsonNode& node ) const
	{
		if ( ( expressionIndex < 0 ) || ( expressionIndex >= static_cast<int> ( expressions.size () ) ) )
		{
			return false;
		}

		const Expression& expression = expressions [ static_cast<size_t> ( expressionIndex ) ];

		switch ( expression.kind )
		{
			case ExpressionKind::Or:
			{
				return test_expression ( expression.left, node ) || test_expression ( expression.right, node );
			}

			case ExpressionKind::And:
			{
				return test_expression ( expression.left, node ) && test_expression ( expression.right, node );
			}

			case ExpressionKind::Not:
			{
				return !test_expression ( expression.left, node );
			}

			case ExpressionKind::Comparison:
			{
				return compare_values
				(
					operand_value ( expression.leftOperand,  node ),
					expression.comparison,
					operand_value ( expression.rightOperand, node )
				);
			}

			case ExpressionKind::Existence:
			{
				return value_is_truthy ( operand_value ( expression.leftOperand, node ) );
			}
		}

		return false;
	}

	void JsonPathQuery::collect_self_and_descendants ( const JsonNode& node, std::vector<const JsonNode*>& out )
	{
		out.push_back ( &node );

		if ( node.kind () == JsonKind::Array )
		{
			const int elementCount = node.array_size ();

			for ( int index = 0; index < elementCount; ++index )
			{
				const JsonNode* const element = node.array_element ( index );

				if ( element != nullptr )
				{
					collect_self_and_descendants ( *element, out );
				}
			}
		}
		else if ( node.kind () == JsonKind::Object )
		{
			const int memberCount = node.member_count ();

			for ( int index = 0; index < memberCount; ++index )
			{
				const JsonNode* const member = node.member_value ( index );

				if ( member != nullptr )
				{
					collect_self_and_descendants ( *member, out );
				}
			}
		}
	}

	const JsonNode* JsonPathQuery::resolve_relative_path ( const std::vector<PathStep>& steps, const JsonNode& node )
	{
		const JsonNode* current = &node;

		for ( const PathStep& step : steps )
		{
			if ( current == nullptr )
			{
				return nullptr;
			}

			if ( step.isIndex )
			{
				if ( current->kind () != JsonKind::Array )
				{
					return nullptr;
				}

				current = current->array_element ( step.index );
			}
			else
			{
				if ( current->kind () != JsonKind::Object )
				{
					return nullptr;
				}

				current = current->find_member ( step.name );
			}
		}

		return current;
	}
}

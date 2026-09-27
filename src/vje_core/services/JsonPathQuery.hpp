//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPathQuery -- the JSONPath engine (QUERY-01..07, spec section 3.7). A query is COMPILED once into a list of
//   segments and then EVALUATED against a JsonNode tree, returning the nodes it names as JsonPointers.
//
//   IT RETURNS POINTERS, AND THAT IS WHY IT IS HAND-ROLLED. What makes the query view useful is that a result IS a
//   selection: choosing one publishes it and the tree reveals the node. A third-party evaluator returns values, or its
//   own node handles, either of which would have to be mapped back to a POSITION in a document the user is free to edit
//   underneath it. Producing pointers directly removes that mapping rather than making it someone's job. Hand-rolling
//   also keeps the one-external-library policy (yaml-cpp) that the CSV and XML converters are already written to.
//
//   THE ACCEPTED SUBSET IS STATED, NOT IMPLIED. JSONPath has no single normative grammar, so spec section 2.7's table is
//   the definition and this file implements exactly it: root, child by name and by bracketed name, index (negative from
//   the end), slice (positive step only), wildcard, recursive descent, union, and the bounded filter expression of
//   QUERY-03. Everything else -- script expressions, functions, regular expressions, arithmetic, a filter or a wildcard
//   inside a filter's own path -- is a compile ERROR carrying a position, never a silent no-op (QUERY-04). An engine that
//   quietly ignores what it does not understand answers a question the user did not ask.
//
//   RESULTS ARE IN DOCUMENT ORDER WITH NO DUPLICATES (QUERY-06). Evaluation produces a SET of nodes; the pointers are
//   then produced by one pre-order walk of the tree, emitting a node when it is in that set. Order therefore comes from
//   the document rather than from the order the selectors happened to visit things in, and a union naming the same node
//   twice -- or a descent reaching it two ways -- is one result. The intermediate node set is deduplicated after every
//   segment as well, which is a CORRECTNESS bound rather than a tidy-up: without it "$..*..*" grows the working set
//   multiplicatively on a document that has no more nodes than it started with.
//
//   A NAME SELECTOR TAKES THE FIRST MATCH when an object carries duplicate keys, because that is what a JSON Pointer
//   means everywhere else in VJE (JsonPointer.hpp). Returning both would produce two results whose pointer text is
//   identical and which therefore both resolve to the same node.
//
//   NUMBERS COMPARE BY VALUE, NOT BY TOKEN. FILE-10's exact lexeme is preserved in the document and is what a save
//   writes; it is not what "@.price < 10" asks about, so a filter comparison converts to double. That is stated in
//   QUERY-03 rather than left to be discovered from a surprising result.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QString>

#include <vector>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// Why a query would not compile, and WHERE (QUERY-05). The span is what the query box selects, so the error is
	// reported at the mistake rather than merely about it; a length of 0 means end-of-input, where there is no text to
	// mark and the caret goes to the end.
	//-----------------------------------------------------------------------------------------------------------------

	struct JsonPathError
	{
		QString message;
		int     position = 0;                              // 0-based character offset into the query text.
		int     length   = 0;                              // Characters spanned; 0 at end-of-input.
	};

	//*****************************************************************************************************************
	// Class: JsonPathQuery
	//*****************************************************************************************************************

	class JsonPathQuery
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		JsonPathQuery () = default;                        // An empty, invalid query; compile() is the way in.

		//=============================================================================================================
		// Factories
		//=============================================================================================================

	public:

		// Compile query text. Always returns an object -- ask is_valid() before evaluating, and error() for the reason
		// and the position. An EMPTY query is invalid with an empty message: it is not a mistake, so there is nothing
		// to report (QUERY-05), and the caller distinguishes the two by the message rather than by a third state.

		static JsonPathQuery compile ( const QString& text );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool                 is_valid () const;
		const JsonPathError& error    () const;
		const QString&       text     () const;

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Every node the query names, as pointers, in DOCUMENT ORDER with no duplicates. An invalid query evaluates to
		// nothing rather than refusing, so a caller that has already reported the error does not have to guard the call.

		std::vector<JsonPointer> evaluate ( const JsonNode& root ) const;

		// Compile and evaluate in one call, for a caller that keeps no compiled query of its own. outError receives the
		// compile error (cleared on success) when it is not null.

		static std::vector<JsonPointer> run ( const JsonNode& root, const QString& text, JsonPathError* outError = nullptr );

		//=============================================================================================================
		// Internals -- the compiled form.
		//
		// Nested and private because they are this class's own representation. Parser is declared here and DEFINED in
		// the .cpp, which is what lets the whole grammar be written in one place without any of this reaching a
		// consumer: a nested class is a member, so it already has access to everything below it.
		//=============================================================================================================

	private:

		struct Parser;

		enum class SelectorKind
		{
			Name,                                          // .name  ['name']  ["name"]
			Index,                                         // [0]  [-1]
			Slice,                                         // [start:end:step]
			Wildcard,                                      // .*  [*]
			Filter                                         // [?( ... )]
		};

		enum class ComparisonOperator
		{
			Equal,
			NotEqual,
			Less,
			LessOrEqual,
			Greater,
			GreaterOrEqual
		};

		enum class OperandKind
		{
			RelativePath,                                  // @  @.a.b  @[0]  @['a b']
			Number,
			String,
			Boolean,
			Null
		};

		// One step of a filter's relative path. Deliberately not a JsonPointer: a pointer's tokens are all strings and
		// an array index is only an index by convention, where here the parser already knows which it wrote.

		struct PathStep
		{
			bool    isIndex = false;
			int     index   = 0;                           // Non-negative; QUERY-04 excludes a negative index here.
			QString name;
		};

		struct Operand
		{
			OperandKind           kind    = OperandKind::Null;
			std::vector<PathStep> pathSteps;               // RelativePath; empty means "@" itself.
			double                number  = 0.0;
			QString               string;
			bool                  boolean = false;
		};

		enum class ExpressionKind
		{
			Or,
			And,
			Not,
			Comparison,
			Existence                                      // A bare operand: present, and neither false nor null.
		};

		// The filter AST, held in a flat pool with integer links rather than by pointer, so a compiled query stays an
		// ordinary copyable value.

		struct Expression
		{
			ExpressionKind     kind       = ExpressionKind::Existence;
			int                left       = -1;            // Index into the pool (Or / And / Not).
			int                right      = -1;
			ComparisonOperator comparison = ComparisonOperator::Equal;
			Operand            leftOperand;
			Operand            rightOperand;
		};

		struct Selector
		{
			SelectorKind kind = SelectorKind::Wildcard;

			QString name;                                  // Name.
			int     index = 0;                             // Index; may be negative (counts from the end).

			bool hasStart = false;                         // Slice; an absent bound means "from the start" / "to the end".
			bool hasEnd   = false;
			int  start    = 0;
			int  end      = 0;
			int  step     = 1;

			int filterRoot = -1;                           // Filter; index into the expression pool.
		};

		// One step of the path. A union is simply more than one selector at the same step, which is what lets the
		// evaluator treat "[0]" and "[0,2]" as the same shape of thing.

		struct Segment
		{
			bool                  descendant = false;      // Preceded by "..".
			std::vector<Selector> selectors;
		};

		//=============================================================================================================
		// Internals -- what a filter operand is worth, and how two of them compare (QUERY-03).
		//
		// Absent and Container are the two kinds a JSON value does not have, and both exist because a filter has to
		// answer for them: a path may name nothing at all, and it may name a thing that is present without being
		// comparable. Folding either into Null would make "@.missing == null" true, which is a different claim.
		//=============================================================================================================

	private:

		enum class ValueKind
		{
			Absent,
			Null,
			Boolean,
			Number,
			String,
			Container
		};

		struct FilterValue
		{
			ValueKind kind    = ValueKind::Absent;
			bool      boolean = false;
			double    number  = 0.0;
			QString   string;
		};

		static FilterValue operand_value ( const Operand& operand, const JsonNode& node );

		static bool compare_values ( const FilterValue& left, ComparisonOperator comparison, const FilterValue& right );

		// A bare operand as an expression: present, and neither false nor null (QUERY-03). A number of 0 and an empty
		// string are both present and therefore both true -- JSON has no falsiness, and inventing one here would make
		// "[?(@.count)]" quietly skip the zeroes.

		static bool value_is_truthy ( const FilterValue& value );

		//=============================================================================================================
		// Internals -- evaluation.
		//=============================================================================================================

	private:

		void apply_segment ( const Segment& segment, std::vector<const JsonNode*>& nodes ) const;

		void apply_selector ( const Selector& selector, const JsonNode& node, std::vector<const JsonNode*>& out ) const;

		// Does this filter expression hold for a candidate node? Recursive over the pool; an out-of-range index is
		// false, which is what makes a malformed pool inert rather than undefined.

		bool test_expression ( int expressionIndex, const JsonNode& node ) const;

		// Node and every descendant, pre-order. Used by "..", which selects from the node itself as well as below it.

		static void collect_self_and_descendants ( const JsonNode& node, std::vector<const JsonNode*>& out );

		// The node a filter's relative path names, or nullptr when it names nothing (QUERY-03's "resolves to nothing").

		static const JsonNode* resolve_relative_path ( const std::vector<PathStep>& steps, const JsonNode& node );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QString       queryText;
		JsonPathError compileError;
		bool          valid = false;

		std::vector<Segment>    segments;
		std::vector<Expression> expressions;
	};
}

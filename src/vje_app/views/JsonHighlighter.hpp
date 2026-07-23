//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonHighlighter -- the Code View's JSON syntax colouring (EDITOR-07), a QSyntaxHighlighter driven by the shared
//   JsonLexer.
//
//   IT RE-LEXES ONE BLOCK AT A TIME, AND THAT IS SOUND rather than an approximation, because RFC 8259 forbids a raw
//   newline inside a string literal: no JSON token can span a line. So a line can be tokenized without any knowledge of
//   the lines around it, which is what lets QSyntaxHighlighter's incremental, per-block re-highlighting hold up on a
//   large document (NFR-03) -- editing one line re-lexes that line and nothing else.
//
//   THE ONE PIECE OF CONTEXT IT NEEDS is whether a string is a member NAME or a string VALUE, which is not a property
//   of the token but of what FOLLOWS it: a ':' makes it a name. That lookahead stays within the block, so a member
//   whose key and colon somehow ended up on different lines colours as a value. Every line JsonFormatter emits keeps
//   them together, and hand-written JSON that splits them is vanishingly rare -- worth stating, not worth carrying
//   cross-block state for, which would mean re-highlighting the PREVIOUS block on a change and is the one thing
//   QSyntaxHighlighter's design does not do.
//
//   Invalid spans are left uncoloured deliberately. Text is invalid for most of the time a user spends typing in it,
//   and colouring a half-typed string as an error paints the document red while somebody is simply mid-word; the
//   validation message beneath the editor is where an error is reported (EDITOR-07).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "style/CodeTokenPalette.hpp"

#include <QSyntaxHighlighter>

class QTextDocument;

namespace vje
{
	//*****************************************************************************************************************
	// Class: JsonHighlighter
	//*****************************************************************************************************************

	class JsonHighlighter : public QSyntaxHighlighter
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonHighlighter ( QTextDocument* document );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Re-colour against a new palette. Calls rehighlight(), so it is the theme-change entry point rather than
		// something to call per keystroke.

		void set_token_palette ( const CodeTokenPalette& tokens );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		const CodeTokenPalette& token_palette () const;

		//=============================================================================================================
		// QSyntaxHighlighter
		//=============================================================================================================

	protected:

		void highlightBlock ( const QString& text ) override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		CodeTokenPalette tokens;
	};
}

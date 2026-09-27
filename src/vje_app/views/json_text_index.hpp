//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_text_index -- the map between the Code View's TEXT and the document's nodes, in both directions:
//
//     pointer -> line     where a node's text begins, which is how a tree selection reveals itself in the editor
//                         (EDITOR-07: the tree selection positions the caret / current-line highlight at the
//                         corresponding element).
//     line    -> pointer  which node a line belongs to, which is how a double-click in the editor names a node to the
//                         rest of the application.
//
//   WHY IT READS THE TEXT AND NOT THE DOCUMENT. The obvious implementation is to have JsonFormatter record a line as it
//   emits each node, since the formatter is what decides where everything lands. That works exactly until the user
//   types, which is the entire point of the view: from the first keystroke the buffer is no longer formatter output, so
//   a formatter-derived map describes a document the editor is not showing. EDITOR-09 requires tree navigation during
//   an uncommitted edit to stay useful ("keeps the in-progress text and only moves the caret within it"), and that is
//   only possible from an index of the text ACTUALLY ON SCREEN. Scanning is also what makes the map survive the user's
//   own hand-formatting, which no profile describes.
//
//   It is a structural scan over the shared JsonLexer -- it walks the grammar to keep track of the path, but decodes no
//   values and builds no tree, so it stays cheap enough to run on every commit and every re-render.
//
//   TOLERANT BY CONSTRUCTION. The text is invalid for most of the time a user spends typing in it. The scan stops at
//   the first malformed or unexpected token and returns everything mapped up to that point, so the tree keeps
//   navigating the part of the document above the error -- which is where the user is not, and therefore exactly the
//   part they still want to reach.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QHash>
#include <QString>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The lines one node occupies, inclusive and 1-BASED. Both ends are held because they answer different questions.
	//
	// The FIRST line is where the node begins, which for an object member is the line its KEY is on: "name": "Bob" is
	// one row to the user, and revealing the value while leaving the key off screen would be a surprising answer to
	// selecting it. Under Allman a container member's key and its opening brace are on different lines; the key's line
	// is the one that names the node, so that is the one recorded.
	//
	// The LAST line is what makes the reverse lookup right. A click on a container's CLOSING brace belongs to the
	// container -- and only a span can say so, because with start lines alone the nearest entry above a closing brace is
	// the container's last child, which is a confidently wrong answer to "what did I just click on?".
	//
	// A degenerate span (first == last) is an ordinary scalar, an inline empty container, or -- see the tolerance note
	// above -- a container whose scan did not complete.
	//
	// The OPEN line is the third answer, added for folding (Phase 15k): the line the node's VALUE begins on, which for a
	// container is its opening bracket. It equals the first line except for a member whose value starts on a later line
	// than its key -- a container member under Allman, whose brace sits on the line below the key. A fold hides what
	// lies BETWEEN the brackets, so it is measured from here rather than from the key.
	//-----------------------------------------------------------------------------------------------------------------

	struct LineSpan
	{
		int firstLine = 0;
		int lastLine  = 0;
		int openLine  = 0;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Keyed by JsonPointer::to_string() rather than by JsonPointer, because a QHash needs a qHash overload and the
	// pointer's text form is already its canonical identity.
	//-----------------------------------------------------------------------------------------------------------------

	using PointerSpanIndex = QHash<QString, LineSpan>;

	// outComplete reports whether the scan reached the END of the text -- one whole value and nothing after it -- rather
	// than stopping at an error. A partial index is still right about everything above the error, which is enough to
	// navigate by, but it is silent about everything below it, so a caller that would act on an ABSENCE (folding, which
	// drops a fold whose node has gone) must ask. It may be null where the caller does not care.

	PointerSpanIndex build_pointer_span_index ( const QString& jsonText, bool* outComplete = nullptr );

	//-----------------------------------------------------------------------------------------------------------------
	// pointer -> line.
	//-----------------------------------------------------------------------------------------------------------------

	// The line a node's text begins on, or 0 when the index does not hold it (an unparsed region, or a node the edited
	// text no longer contains). 0 is used rather than -1 because lines are 1-based, so it cannot collide with a real
	// answer.

	int line_for_pointer ( const PointerSpanIndex& index, const JsonPointer& pointer );

	//-----------------------------------------------------------------------------------------------------------------
	// line -> pointer.
	//-----------------------------------------------------------------------------------------------------------------

	// The node a line most closely belongs to: the DEEPEST node whose span contains it. A member's row answers with the
	// member, a container's opening or closing brace with the container, and a line the text does not cover (a trailing
	// blank line, or anything below a syntax error) with nothing.
	//
	// outFound distinguishes "nothing" from "the root", which are otherwise both a root pointer. It may be null where
	// the caller does not care.

	JsonPointer pointer_at_line ( const PointerSpanIndex& index, int line, bool* outFound = nullptr );
}

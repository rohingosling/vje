//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   json_text_index -- "which LINE of this JSON text is the node named by this pointer on?" (EDITOR-07: the tree
//   selection positions the caret / current-line highlight at the corresponding element).
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

#include <QHash>
#include <QString>

namespace vje
{
	class JsonPointer;

	//-----------------------------------------------------------------------------------------------------------------
	// A pointer-text -> 1-based line index. Keyed by JsonPointer::to_string() rather than by JsonPointer because a
	// QHash needs a qHash overload and the pointer's text form is already its canonical identity.
	//
	// The line recorded for a node is where its text BEGINS, which for an object member is the line its KEY is on --
	// "name": "Bob" is one row to the user, and revealing the value while leaving the key off screen would be a
	// surprising answer to selecting it. Under Allman a container member's key and its opening brace are on different
	// lines; the key's line is the one that names the node, so that is the one recorded.
	//-----------------------------------------------------------------------------------------------------------------

	using PointerLineIndex = QHash<QString, int>;

	PointerLineIndex build_pointer_line_index ( const QString& jsonText );

	// The line for a pointer, or 0 when the index does not hold it (an unparsed region, or a node the text does not
	// contain). 0 is used rather than -1 because lines here are 1-based, so it cannot collide with a real answer.

	int line_for_pointer ( const PointerLineIndex& index, const JsonPointer& pointer );
}

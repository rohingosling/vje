//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   code_folding -- the Code View's folding decisions (EDITOR-23), as pure functions over line numbers.
//
//   FOLDING IS NOT A NEW PARSE. json_text_index already holds a line span per node -- built in Phase 10 so a double
//   click on a closing brace could name its container -- and a span is exactly what a fold needs: the line the opening
//   bracket is on, where the marker goes, and the line the closing bracket is on. What a fold hides is what lies
//   BETWEEN them. Both bracket lines stay visible, so a folded object still reads as an object, and its closing bracket
//   is still there to be double-clicked.
//
//   A FOLD IS NAMED BY ITS NODE'S POINTER, the same identity TreeViewPane uses to carry expansion across a projection
//   rebuild (TREE-07), because a line number stops meaning the same node the moment anything above it changes. The
//   widget holds the fold state; these functions answer what that state means for each line.
//
//   Kept out of the widget for the reason code_indentation is: the edge cases -- two containers opening on one line, a
//   fold inside a fold, a line inside two folds at once -- are stated here where a test can reach them without a
//   QTextDocument.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/json_text_index.hpp"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One foldable node. Lines are 1-BASED, as json_text_index's are. The hidden interior is the lines strictly between
	// openLine and closeLine, so a region exists only where there is at least one such line -- "{ }" on one line, or a
	// container whose brackets sit on adjacent lines, has nothing to fold.
	//-----------------------------------------------------------------------------------------------------------------

	struct FoldRegion
	{
		QString pointer;          // JsonPointer::to_string () -- the fold's identity.
		int     openLine  = 0;    // The opening bracket's line, where the gutter marker is drawn.
		int     closeLine = 0;    // The closing bracket's line.
	};

	// Every foldable node, in order of openLine. One region per marker line: where two containers open on the same line
	// -- "[ {" written by hand -- the OUTER one keeps the marker, since a line has room for one and the outer fold hides
	// the inner one anyway.

	QList<FoldRegion> fold_regions ( const PointerSpanIndex& index );

	// Which lines a set of folds leaves visible: element i answers for line i + 1, for lines 1..lineCount. A line hidden
	// by several folds at once stays hidden until all of them open.

	QList<bool> visible_lines ( int lineCount, const QList<FoldRegion>& regions, const QSet<QString>& folded );

	// The folds that hide a line -- every one that must open to reveal it, outermost first. Empty when the line is
	// already visible.

	QStringList folds_hiding_line ( const QList<FoldRegion>& regions, const QSet<QString>& folded, int line );

	// The innermost region whose brackets enclose a line (openLine <= line <= closeLine) and whose fold state is the one
	// asked for, as an index into regions -- or -1. What the fold and unfold keys act on: fold takes the innermost OPEN
	// region around the caret, so pressing it again folds the next one out.

	int innermost_region_at_line ( const QList<FoldRegion>& regions, const QSet<QString>& folded, int line, bool wantFolded );
}

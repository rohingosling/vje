//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   value_placeholders -- how a CONTAINER is denoted where only one line, one cell, or one field is available.
//
//   WHY THESE ARE STATED HERE AND NOWHERE ELSE. An object or an array has no one-line form of its own, so every surface
//   that renders a JSON value into a single slot has to invent one -- and three of them do: the Text View's key-value
//   listing (TextViewRenderer), the Form View's two grids (cell_presentation), and CSV export (CsvCodec, FILE-11).
//   Those are not three independent choices that happen to agree: the CSV rule is written as "the same text the table
//   view shows", so the day one of them changed, a file would stop matching the screen it was exported from. One
//   constant is what makes that a definition rather than a coincidence.
//
//   THEY ARE PRESENTATION, NOT NOTATION -- deliberately not JSON. `{...}` is not valid JSON and is not meant to be:
//   it says "there is a structure here that does not fit", which is exactly what the eye needs in a cell and exactly
//   what the Code View exists to show properly. A consequence worth stating: a CSV cell carrying one of these is
//   LOSSY, and re-importing that file reads it back as the four-character string, not as the container it stood for.
//
//   The scalar spellings (`null`, `true`, `false`, a number's raw token) are NOT here, because they are not a choice --
//   they are JSON's own notation, and a surface writing something else would be wrong rather than inconsistent.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje::value_placeholders
{
	inline const QString OBJECT = QStringLiteral ( "{...}" );
	inline const QString ARRAY  = QStringLiteral ( "[...]" );
}

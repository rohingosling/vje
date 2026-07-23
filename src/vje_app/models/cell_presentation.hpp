//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   cell_presentation -- how ONE JSON value appears in a Form View grid, shared verbatim by the object form
//   (EDITOR-02) and the array table (EDITOR-03). Both specs describe the same presentation in the same words -- plain
//   text at rest, "{...}" / "[...]" for containers in the drill-in colour, a dimmed read-only "null" placeholder -- so
//   they are one definition here rather than two that could drift.
//
//   The MISSING content kind has no JsonNode behind it: it is the empty cell a ragged array shows where an element
//   lacks the column's member (EDITOR-03). It is a real, landable, addressable cell that simply has no value yet, which
//   is why it is a content kind rather than an absent cell.
//
//   The custom item roles below are what let one delegate serve both models: the delegate asks the model what KIND of
//   content a cell holds and which JSON kind its value is, and never needs to know whether it is looking at a form
//   field or a table cell.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>

#include <QString>
#include <Qt>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// What a grid cell is showing. This drives the delegate's colouring and its choice of editor -- NOT whether the cell
	// can take the highlight, which every cell can (see grid_navigation.hpp).
	//-----------------------------------------------------------------------------------------------------------------

	enum class CellContent
	{
		Scalar,        // A string, number, or boolean: editable in place (EDITOR-02 / 03).
		Null,          // A null value: a dimmed READ-ONLY placeholder in Phase 7 (typed entry into it is EDITOR-12).
		Container,     // An object or array: shown as "{...}" / "[...]"; activating it drills in (EDITOR-05).
		Missing        // A ragged array's absent member: an empty cell, landable, with nothing to edit yet.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Custom item roles both Form View models answer. The delegate reads these instead of down-casting the model, which
	// is what keeps one delegate serving the form and the table.
	//-----------------------------------------------------------------------------------------------------------------

	namespace cell_roles
	{
		inline constexpr int CONTENT_KIND = Qt::UserRole + 1;   // CellContent, as an int.
		inline constexpr int VALUE_KIND   = Qt::UserRole + 2;   // JsonKind, as an int; absent for CellContent::Missing.

		// Does this cell hold a member KEY rather than a value (EDIT-02, editable in the object form)? The delegate
		// needs to know because a key edits under different rules from a string value: no number validation, and a
		// rename that would collide with a sibling cannot be committed.

		inline constexpr int IS_KEY_CELL  = Qt::UserRole + 3;   // bool; absent (and therefore false) on a value cell.

		// The keys a rename must not collide with -- the object's other members. Supplied by the model so the delegate
		// stays ignorant of which model it is driving, the same way CONTENT_KIND already keeps it ignorant of kinds.

		inline constexpr int RIVAL_KEYS   = Qt::UserRole + 4;   // QStringList; meaningful only on a key cell.
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The literal placeholder texts. Named because they are a presentation contract the Text View's renderer states in
	// the same words and the tests match on.
	//-----------------------------------------------------------------------------------------------------------------

	namespace cell_text
	{
		inline const QString OBJECT_PLACEHOLDER = QStringLiteral ( "{...}" );
		inline const QString ARRAY_PLACEHOLDER  = QStringLiteral ( "[...]" );
		inline const QString NULL_PLACEHOLDER   = QStringLiteral ( "null" );

		inline const QString BOOLEAN_TRUE  = QStringLiteral ( "true" );
		inline const QString BOOLEAN_FALSE = QStringLiteral ( "false" );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Classification and display. A null node pointer means the MISSING cell -- the one content kind with no value
	// behind it -- so callers projecting a ragged row need no special case of their own.
	//-----------------------------------------------------------------------------------------------------------------

	CellContent cell_content ( const JsonNode* node );

	// The cell's text at rest. Strings show their content unquoted, numbers their RAW token (FILE-10: never a
	// reformatted conversion), booleans "true" / "false", null the placeholder, containers their ellipsis, and a
	// missing cell nothing at all.

	QString cell_display_text ( const JsonNode* node );

	// The text an editor opens with. Identical to the display text for the editable kinds; empty for everything else,
	// none of which opens an editor in Phase 7.

	QString cell_edit_text ( const JsonNode* node );

	// Whether activating this cell opens an editor (EDITOR-02 / 03). Scalars other than null; nothing else. Null and
	// missing cells become editable with the EDITOR-12 typed-entry rule in Phase 9.

	bool is_editable_cell ( const JsonNode* node );

	// Whether activating this cell DRILLS IN instead of editing (EDITOR-05).

	bool is_drill_in_cell ( const JsonNode* node );
}

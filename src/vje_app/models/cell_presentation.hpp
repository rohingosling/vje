//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <vje_core/services/json_escapes.hpp>
#include <vje_core/services/value_placeholders.hpp>

#include <memory>

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

		// Does an editor opened on this cell speak ESCAPED notation (SET-03)? The delegate needs it to decide whether a
		// malformed escape should refuse the commit, and asks the model for the same reason it asks CONTENT_KIND: so it
		// never has to know which model it is driving, nor read a setting of its own.

		inline constexpr int ESCAPED_NOTATION = Qt::UserRole + 5;   // bool; meaningful only on an editable string cell.

		// What this cell is CALLED, for the accessible name of an editor opened on it (NFR-05). An editor is a bare
		// QLineEdit or QComboBox parented into a viewport, so it inherits no name from the cell it covers and announces
		// nothing at all -- and it is the control the user spends most of their time inside.
		//
		// Answered by the model, for the reason every role here is: the delegate drives two grids and must not learn
		// which. The object form answers with the row's member key, the array table with the column's key -- in both
		// cases the label the user can SEE beside or above the cell, so what is announced is what is on screen.

		inline constexpr int CELL_LABEL = Qt::UserRole + 6;   // QString; the key or column this cell sits under.
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The literal placeholder texts.
	//
	// The two CONTAINER placeholders are aliases of the shared ones, not copies: the Text View's renderer and CSV
	// export write the same two strings, and FILE-11's rule is literally "the text the table view shows", so a second
	// spelling here would be the thing that lets a file stop matching the screen it came from
	// (vje_core/services/value_placeholders.hpp says why). The scalar spellings below are JSON's own notation and are
	// stated here because nothing outside a grid needs them.
	//-----------------------------------------------------------------------------------------------------------------

	namespace cell_text
	{
		inline const QString& OBJECT_PLACEHOLDER = value_placeholders::OBJECT;
		inline const QString& ARRAY_PLACEHOLDER  = value_placeholders::ARRAY;
		inline const QString  NULL_PLACEHOLDER   = QStringLiteral ( "null" );

		inline const QString BOOLEAN_TRUE  = QStringLiteral ( "true" );
		inline const QString BOOLEAN_FALSE = QStringLiteral ( "false" );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Classification and display. A null node pointer means the MISSING cell -- the one content kind with no value
	// behind it -- so callers projecting a ragged row need no special case of their own.
	//-----------------------------------------------------------------------------------------------------------------

	CellContent cell_content ( const JsonNode* node );

	// The cell's text at rest. Strings show their content unquoted and through the SET-03 display mode, numbers their
	// RAW token (FILE-10: never a reformatted conversion), booleans "true" / "false", null the placeholder, containers
	// their ellipsis, and a missing cell nothing at all.
	//
	// The mode is explicit rather than defaulted, so a new call site has to decide which notation it means rather than
	// silently inheriting one.

	QString cell_display_text ( const JsonNode* node, StringDisplay stringDisplay );

	// The text an editor opens with. Identical to the display text for every kind except a string under the Flattened
	// mode, which edits in Escaped notation because it cannot be edited losslessly in its own (json_escapes.hpp).
	// Empty for everything that does not open an editor.

	QString cell_edit_text ( const JsonNode* node, StringDisplay stringDisplay );

	// Whether activating this cell opens an editor (EDITOR-02 / 03). Scalars other than null; nothing else. Null and
	// missing cells become editable with the EDITOR-12 typed-entry rule in Phase 9.

	// EDITOR-12's typed entry, read through the SET-03 notation. A null, missing or provisional cell has no value to
	// edit, so what is typed into it is interpreted as a JSON literal -- a number, true / false / null, or a quoted
	// string -- and anything else commits as a plain string.
	//
	// It is that LAST case the notation belongs to, and it was missing until the 2026-07-28 review: typing a	b into a
	// filled string cell stored a tab while the identical keystrokes into the empty cell beside it stored four literal
	// characters, and the cell then redisplayed with a doubled backslash. The literal rule is untouched -- 123 is still
	// a number and true still a boolean -- and a QUOTED entry is left alone too, because JsonParser has already decoded
	// it.
	//
	// Returns null only if the text cannot be committed at all, which today means a malformed escape in escaped
	// notation; the caller refuses the commit exactly as it does for a malformed number (VAL-03).

	std::unique_ptr<JsonNode> typed_entry_value ( const QString& text, StringDisplay stringDisplay );

	bool is_editable_cell ( const JsonNode* node );

	// Whether activating this cell DRILLS IN instead of editing (EDITOR-05).

	bool is_drill_in_cell ( const JsonNode* node );
}

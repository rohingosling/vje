//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_escapes -- the codebase's single JSON string escape table, and the three ways a string value may be presented
//   to the user on top of it (SET-03, architecture.md section 4.6).
//
//   THE CORRECTION THAT SHAPES ALL OF THIS. A JsonNode string holds DECODED characters: JsonParser::decode_string
//   resolves the escapes at load and JsonSerializer::write_string re-escapes at save, so "\t" exists on disk and nowhere
//   in the model. String display is therefore purely a PRESENTATION setting -- it touches no node, no parser and no
//   serializer, and cannot affect round-trip fidelity (FILE-04 / FILE-10). It also has nothing to say about the Code
//   View, which renders JSON source, where escapes are literal by definition.
//
//   ONE TABLE, SHARED. Both JsonParser::decode_string and JsonSerializer::write_string delegate here, for exactly the
//   reason decode_string was made public in Phase 8: a second copy of the escape table is a second copy that can
//   disagree. decode() takes a QStringView so the parser's per-string-token path allocates nothing on the way in.
//
//   THE MODE IS THE NOTATION, and that one sentence is the whole design. string_commit_value is the exact inverse of
//   string_edit_text under the same mode, which is what makes a file loaded containing "a\tb" and a paste of the four
//   characters a\tb indistinguishable -- both are text arriving in a field whose notation is escaped. Guessing whether
//   pasted text "was meant as" escaped is deliberately not attempted: it is unimplementable in general and would make a
//   literal backslash-t impossible to enter.
//
//   TWO TOLERANCES ARE DELIBERATE. A RAW control character inside escaped text decodes as itself, so a pasted tab and a
//   typed \t both land on a TAB and both reappear escaped once committed. A MALFORMED escape fails, and the caller
//   surfaces that as QValidator::Intermediate rather than Invalid -- the keystroke is allowed and the COMMIT is refused
//   with the caret held, reusing VAL-03's existing path (lessons-learned.md Q14).
//
//   SCOPE: the eight two-character escapes. \uXXXX is decoded (a file may contain it) but never PRODUCED, so an accented
//   character always shows as itself -- which is also what a save writes, since the serializer escapes only below
//   U+0020.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringView>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The escape table itself. "Body" throughout means a JSON string WITHOUT its surrounding quotes.
	//-----------------------------------------------------------------------------------------------------------------

	namespace json_escapes
	{
		// Is this a character JSON may not carry raw inside a string, and that flatten() removes? The C0 range, which is
		// also the threshold JsonSerializer escapes at -- stated once so the two cannot drift.

		bool is_control ( QChar character );

		// Decoded value -> escaped body. Produces the eight two-character escapes, and \u00XX for any other control
		// character; everything else, including non-ASCII, passes through unchanged.

		QString escape ( QStringView value );

		// Escaped body -> value. TOTAL, unlike JsonParser::decode_string, which may assume a well-formed token: this one
		// is handed arbitrary text a user is part-way through typing. Returns false on a malformed or unfinished escape
		// and reports its offset, leaving out holding everything decoded up to that point.
		//
		// A raw control character is NOT an error here (see the header): it decodes as itself.

		bool decode ( QStringView body, QString& out, int* errorOffset = nullptr );

		// Every control character removed outright -- SET-03's Flattened mode. Lossy by design and therefore never an
		// editing notation.

		QString flatten ( QStringView value );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// How a string value is shown, and typed (SET-03). One setting drives the Form View and the Text View, so the two
	// tabs never disagree about the same node (EDITOR-06).
	//-----------------------------------------------------------------------------------------------------------------

	enum class StringDisplay
	{
		Escaped,            // "a\tb" -- the default. Lossless and unambiguous; the notation the editor speaks.
		Decoded,            // The real characters. \t typed here is a backslash and a t, because the field is raw text.
		Flattened           // Control characters removed. Display only -- see string_edit_text.
	};

	// The text shown AT REST.

	QString string_display_text ( const QString& value, StringDisplay mode );

	// The text an editor OPENS with. Identical to the display text except under Flattened, which falls back to Escaped:
	// a cell showing "field123" gives a commit no way to know where the tab went, so it cannot be its own edit notation.
	// The visible consequence -- the cell changes as the editor opens -- is stated in spec section 2.10 rather than left
	// to be discovered.

	QString string_edit_text ( const QString& value, StringDisplay mode );

	// Editor text -> the value to store, and whether it may be stored at all. False means a malformed escape: the caller
	// refuses the commit and keeps the editor open, exactly as a malformed number does (VAL-03).
	//
	// Decoded mode always succeeds -- there is no notation to get wrong.

	bool string_commit_value ( const QString& text, StringDisplay mode, QString& outValue, int* errorOffset = nullptr );

	// The notation an EDITOR in this mode speaks -- Escaped for both Escaped and Flattened. Named because three call
	// sites ask the same question and "mode == Decoded" reads as a coincidence rather than a rule.

	bool mode_edits_in_escaped_notation ( StringDisplay mode );
}

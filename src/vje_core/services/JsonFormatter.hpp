//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonFormatter -- the pure JsonNode -> text pretty-printer implementing the DOCUMENT FORMAT PROFILE (SET-07 /
//   FILE-03): indent character/size, brace style (K&R / Allman), and align-name-separators. It is consumed by both
//   the save path (DocumentIo) and the Code View, so the displayed text is byte-for-byte what File > Save writes
//   (EDITOR-07). Number nodes are emitted as their raw token verbatim (FILE-10); empty
//   containers render inline as {} / [] in both brace styles.
//
//   The two brace styles differ ONLY for a non-empty container that is an object member's value: K&R keeps the
//   opening brace on the member's line ("profile": {), Allman drops it to its own line at the member's indent. An
//   array element or the root has no key to hug, so its opening brace sits at its own indent in both styles.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// The document format profile (SET-07). The formatting subset shared verbatim between the Code View's displayed
	// text and File > Save. Defaults match the spec: 2 spaces, Allman, separators not aligned.
	//-----------------------------------------------------------------------------------------------------------------

	enum class IndentKind
	{
		Spaces,             // indentSize spaces per level.
		Tabs                // One tab per level (indentSize ignored).
	};

	enum class BraceStyle
	{
		KAndR,              // Opening brace hugs the member's line ("profile": {).
		Allman              // Opening brace on its own line at the member's indent.
	};

	struct FormatProfile
	{
		IndentKind indent              = IndentKind::Spaces;
		int        indentSize          = 2;                    // Spaces per level; clamped to >= 1 when Spaces.
		BraceStyle braceStyle          = BraceStyle::Allman;
		bool       alignNameSeparators = false;                // Pad member names so the ':' separators align.
	};

	//*****************************************************************************************************************
	// Class: JsonFormatter
	//*****************************************************************************************************************

	class JsonFormatter
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Pretty-print the whole subtree under the profile. No trailing newline (the save path appends the single
		// trailing LF, FILE-03).

		static QString format ( const JsonNode& node, const FormatProfile& profile );

		//=============================================================================================================
		// Internals
		//=============================================================================================================

	private:

		// Write node's value with its opening brace (if any) at the current cursor. containerLevel is the indent
		// level of this node's own braces; its children sit at containerLevel + 1.

		static void write_value ( const JsonNode& node, QString& out, int containerLevel, const FormatProfile& profile );

		static QString indent_text ( int level, const FormatProfile& profile );
		static bool    is_non_empty_container ( const JsonNode& node );
	};
}

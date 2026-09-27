//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   code_indentation implementation. See the header for why outdent is tolerant of whitespace the profile did not
//   produce and indent is not.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/code_indentation.hpp"

#include <QStringList>

#include <algorithm>

namespace vje
{
	namespace
	{
		// The profile's indentSize is only meaningful for Spaces, but an outdent has to answer "how many leading spaces
		// is one level?" under BOTH profiles -- a Tabs document can still contain space-indented lines somebody pasted
		// in. The stored size is the best available answer either way.

		int space_width ( const FormatProfile& profile )
		{
			return ( profile.indentSize >= 1 ) ? profile.indentSize : 1;
		}

		bool is_indent_character ( QChar character )
		{
			return ( character == QLatin1Char ( ' ' ) ) || ( character == QLatin1Char ( '\t' ) );
		}

		bool is_opening_bracket ( QChar character )
		{
			return ( character == QLatin1Char ( '{' ) ) || ( character == QLatin1Char ( '[' ) );
		}

		bool is_closing_bracket ( QChar character )
		{
			return ( character == QLatin1Char ( '}' ) ) || ( character == QLatin1Char ( ']' ) );
		}

		//-------------------------------------------------------------------------------------------------------------
		// What the text before the caret does to the nesting level: its net count of brackets, ignoring those at the
		// start of the line and any inside a string -- and whether it ENDS with an opening bracket, which is the one case
		// where a closing bracket after the caret is that bracket's partner.
		//-------------------------------------------------------------------------------------------------------------

		struct BracketBalance
		{
			int  net             = 0;
			bool endsWithOpening = false;
		};

		BracketBalance bracket_balance ( const QString& text )
		{
			BracketBalance balance;

			int position = 0;

			// The leading closers, with the whitespace and separators between them: "}, ]" at the start of a line is
			// already accounted for by where the line is indented.

			while ( ( position < text.length () )
			     && (    is_closing_bracket  ( text.at ( position ) )
			          || is_indent_character ( text.at ( position ) )
			          || ( text.at ( position ) == QLatin1Char ( ',' ) ) ) )
			{
				++position;
			}

			bool insideString = false;
			bool escaped      = false;

			for ( ; position < text.length (); ++position )
			{
				const QChar character = text.at ( position );

				if ( insideString )
				{
					if ( escaped )
					{
						escaped = false;
					}
					else if ( character == QLatin1Char ( '\\' ) )
					{
						escaped = true;
					}
					else if ( character == QLatin1Char ( '"' ) )
					{
						insideString = false;
					}

					balance.endsWithOpening = false;

					continue;
				}

				if ( character == QLatin1Char ( '"' ) )
				{
					insideString = true;
				}
				else if ( is_opening_bracket ( character ) )
				{
					++balance.net;
				}
				else if ( is_closing_bracket ( character ) )
				{
					--balance.net;
				}

				balance.endsWithOpening = is_opening_bracket ( character );
			}

			return balance;
		}

		//-------------------------------------------------------------------------------------------------------------
		// Backspace's measurements. Columns rather than characters, because the rule compares a tab-indented caret with
		// a space-indented bracket line as readily as with its own kind -- and a tab occupies one indent size on screen
		// (CodeEditor sets its tab stop to exactly that), so this is the column the user sees.
		//-------------------------------------------------------------------------------------------------------------

		int leading_whitespace_length ( const QString& line )
		{
			int length = 0;

			while ( ( length < line.length () ) && is_indent_character ( line.at ( length ) ) )
			{
				++length;
			}

			return length;
		}

		int indent_columns ( const QString& whitespace, const FormatProfile& profile )
		{
			const int width = space_width ( profile );

			int columns = 0;

			for ( const QChar character : whitespace )
			{
				columns = ( character == QLatin1Char ( '\t' ) ) ? ( ( columns / width ) + 1 ) * width : columns + 1;
			}

			return columns;
		}

		// The line's brackets in order, those inside strings left out -- or nothing where the line ends inside a string,
		// which JSON does not allow and which leaves every bracket above it unreadable.

		std::optional<QString> brackets_outside_strings ( const QString& line )
		{
			QString brackets;

			bool insideString = false;
			bool escaped      = false;

			for ( const QChar character : line )
			{
				if ( insideString )
				{
					if ( escaped )
					{
						escaped = false;
					}
					else if ( character == QLatin1Char ( '\\' ) )
					{
						escaped = true;
					}
					else if ( character == QLatin1Char ( '"' ) )
					{
						insideString = false;
					}
				}
				else if ( character == QLatin1Char ( '"' ) )
				{
					insideString = true;
				}
				else if ( is_opening_bracket ( character ) || is_closing_bracket ( character ) )
				{
					brackets.append ( character );
				}
			}

			if ( insideString )
			{
				return std::nullopt;
			}

			return brackets;
		}

		// The structural indent of backspace_width's rule, in columns, or nothing where a line above cannot be read.
		// Reading upwards and each line from its end, a closing bracket is one more to be matched and an opening one
		// matches it; the first opening bracket with nothing left to match is the one the caret's line is inside.

		std::optional<int> structural_indent ( const QString& line, const LineSource& nextLineUp, const FormatProfile& profile )
		{
			const int  indentLength = leading_whitespace_length ( line );
			const bool closesFirst  = ( indentLength < line.length () ) && is_closing_bracket ( line.at ( indentLength ) );

			int unmatchedClosers = 0;

			for ( std::optional<QString> above = nextLineUp (); above.has_value (); above = nextLineUp () )
			{
				const std::optional<QString> brackets = brackets_outside_strings ( *above );

				if ( !brackets.has_value () )
				{
					return std::nullopt;
				}

				for ( int i = static_cast<int> ( brackets->length () ) - 1; i >= 0; --i )
				{
					if ( is_closing_bracket ( brackets->at ( i ) ) )
					{
						++unmatchedClosers;
					}
					else if ( unmatchedClosers > 0 )
					{
						--unmatchedClosers;
					}
					else
					{
						const int openerIndent = indent_columns ( above->left ( leading_whitespace_length ( *above ) ), profile );

						return closesFirst ? openerIndent : openerIndent + space_width ( profile );
					}
				}
			}

			return 0;
		}

		// The base indentation moved by a number of levels: deeper by whole units of the profile, shallower by
		// outdent_width's tolerant rule, so a hand-indented line comes out as far as it can and no further.

		QString shifted_indent ( QString base, int levels, const FormatProfile& profile )
		{
			for ( int level = 0; level < levels; ++level )
			{
				base.append ( indent_unit ( profile ) );
			}

			for ( int level = 0; level > levels; --level )
			{
				base.remove ( 0, outdent_width ( base, profile ) );
			}

			return base;
		}
	}

	QString indent_unit ( const FormatProfile& profile )
	{
		if ( profile.indent == IndentKind::Tabs )
		{
			return QStringLiteral ( "\t" );
		}

		return QString ( space_width ( profile ), QLatin1Char ( ' ' ) );
	}

	int outdent_width ( const QString& line, const FormatProfile& profile )
	{
		if ( line.isEmpty () )
		{
			return 0;
		}

		// A leading tab is one level whatever the profile says, because it is one level to whoever typed it.

		if ( line.at ( 0 ) == QLatin1Char ( '\t' ) )
		{
			return 1;
		}

		const int budget = space_width ( profile );

		int spaces = 0;

		while ( ( spaces < budget ) && ( spaces < line.length () ) && ( line.at ( spaces ) == QLatin1Char ( ' ' ) ) )
		{
			++spaces;
		}

		return spaces;
	}

	QString indent_block ( const QString& block, const FormatProfile& profile )
	{
		const QString unit = indent_unit ( profile );

		QStringList lines = block.split ( QLatin1Char ( '\n' ) );

		for ( QString& line : lines )
		{
			if ( !line.isEmpty () )
			{
				line.prepend ( unit );
			}
		}

		return lines.join ( QLatin1Char ( '\n' ) );
	}

	QString outdent_block ( const QString& block, const FormatProfile& profile )
	{
		QStringList lines = block.split ( QLatin1Char ( '\n' ) );

		for ( QString& line : lines )
		{
			// Per line rather than per block: a block with one flush line among indented ones outdents the rest and
			// leaves that one where it is, which is what a user dragging a selection over a whole object expects.

			line.remove ( 0, outdent_width ( line, profile ) );
		}

		return lines.join ( QLatin1Char ( '\n' ) );
	}

	NewLineEdit indent_for_new_line ( const QString& line, int column, const FormatProfile& profile )
	{
		column = std::clamp ( column, 0, static_cast<int> ( line.length () ) );

		int indentLength = 0;

		while ( ( indentLength < line.length () ) && is_indent_character ( line.at ( indentLength ) ) )
		{
			++indentLength;
		}

		const QString base = line.left ( indentLength );

		// The whitespace either side of the caret is replaced rather than kept (see NewLineEdit).

		int keptEnd = column;

		while ( ( keptEnd > 0 ) && is_indent_character ( line.at ( keptEnd - 1 ) ) )
		{
			--keptEnd;
		}

		int restStart = column;

		while ( ( restStart < line.length () ) && is_indent_character ( line.at ( restStart ) ) )
		{
			++restStart;
		}

		NewLineEdit edit;

		edit.replaceTo = restStart;

		// Nothing but indentation before the caret: the line moves down as it is, and the line it leaves is empty rather
		// than a row of invisible spaces.

		if ( keptEnd == 0 )
		{
			edit.replaceFrom = 0;
			edit.insertion   = QLatin1Char ( '\n' ) + base;
			edit.caretOffset = edit.insertion.length ();

			return edit;
		}

		edit.replaceFrom = keptEnd;

		const BracketBalance balance    = bracket_balance ( line.left ( keptEnd ) );
		const bool           closesNext = ( restStart < line.length () ) && is_closing_bracket ( line.at ( restStart ) );

		if ( closesNext && balance.endsWithOpening )
		{
			const QString inner = shifted_indent ( base, balance.net,     profile );
			const QString outer = shifted_indent ( base, balance.net - 1, profile );

			edit.insertion   = QLatin1Char ( '\n' ) + inner + QLatin1Char ( '\n' ) + outer;
			edit.caretOffset = 1 + inner.length ();

			return edit;
		}

		const int levels = closesNext ? ( balance.net - 1 ) : balance.net;

		edit.insertion   = QLatin1Char ( '\n' ) + shifted_indent ( base, levels, profile );
		edit.caretOffset = edit.insertion.length ();

		return edit;
	}

	int backspace_width ( const QString& line, int column, const LineSource& nextLineUp, const FormatProfile& profile )
	{
		column = std::clamp ( column, 0, static_cast<int> ( line.length () ) );

		// Checked before any line above is read: a Backspace after text is the common case, and it reads nothing.

		if ( ( column == 0 ) || ( leading_whitespace_length ( line ) < column ) )
		{
			return 0;
		}

		const int width        = space_width ( profile );
		const int caretColumns = indent_columns ( line.left ( column ), profile );

		const std::optional<int> structural = structural_indent ( line, nextLineUp, profile );

		const int target = ( structural.has_value () && ( caretColumns > *structural ) )
		                 ? *structural
		                 : ( ( caretColumns - 1 ) / width ) * width;

		int from = column;

		while ( ( from > 0 ) && ( indent_columns ( line.left ( from ), profile ) > target ) )
		{
			--from;
		}

		return column - from;
	}
}

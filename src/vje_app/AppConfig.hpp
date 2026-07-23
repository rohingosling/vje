//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   AppConfig -- the single place a developer looks to tune vje_app at COMPILE TIME.
//
//   These are values a maintainer might reasonably want to adjust while working on the source: layout metrics, look
//   and feel, first-run defaults, and bounded limits. Most are not user-facing; a few (the first-run window size, the
//   recent-files limit) are the built-in defaults behind settings the user can later override at run time through the
//   SettingsStore, which always wins once a persisted value exists.
//
//   WHAT BELONGS HERE
//
//     Cross-cutting numbers that are otherwise scattered across widget construction code and easy to miss -- pane
//     widths, margins, style metrics, render ladders, list caps.
//
//   WHAT DELIBERATELY DOES NOT
//
//     - Anything in vje_core. The core is UI-free and headlessly testable; a shared
//       config header spanning both layers would breach that boundary. JsonParser::MAX_DEPTH stays with the parser.
//     - Format and protocol contracts, which are not tunables. SettingsStore::SCHEMA_VERSION is the settings-file
//       compatibility contract; changing it is a migration, not a preference.
//     - Values with only one meaningful call site and no cross-cutting significance -- resource path prefixes, the
//       command-line argument spellings, the icon geometry table (which lives with its generator).
//
//   COST, STATED PLAINLY: this header is included widely, so editing it rebuilds most of vje_app. That is the accepted
//   trade for discoverability. It is also why the scope above is drawn narrowly -- a dumping ground would both slow
//   builds and separate constants from the code that gives them meaning.
//
//   Sizes are logical pixels; Qt 6 scales them for high-DPI displays automatically, so no manual DPI factor applies.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

namespace vje::config
{
	//-----------------------------------------------------------------------------------------------------------------
	// Main window (NFR-06).
	//-----------------------------------------------------------------------------------------------------------------

	namespace window
	{
		// First-run size only. Superseded by the persisted geometry once one exists.

		inline constexpr int DEFAULT_WIDTH  = 1024;
		inline constexpr int DEFAULT_HEIGHT = 768;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The two-pane workspace (STYLE-01..03).
	//-----------------------------------------------------------------------------------------------------------------

	namespace workspace
	{
		// The master pane cannot be collapsed to nothing.

		inline constexpr int MINIMUM_TREE_PANE_WIDTH = 200;

		// First-run splitter division; superseded by the persisted pane widths (MainWindow::restore_splitter_sizes).

		inline constexpr int INITIAL_TREE_PANE_WIDTH   = 280;
		inline constexpr int INITIAL_EDITOR_PANE_WIDTH = 744;

		// The margin framing the pair of cards. A multiple of the 4 px layout grid (STYLE-03).

		inline constexpr int CONTENT_MARGIN = 8;

		// The inter-card gap, which is what the splitter handle occupies -- STYLE-04 makes the gap itself the
		// separator rather than drawing a bar in it.
		//
		// DERIVED from the margin rather than chosen, because STYLE-03 asks for "consistent 4 px-grid spacing" and the
		// two are the same frame: the gap between the cards and the gap around them read as one measurement, and any
		// second number here would be a chance for them to drift apart.

		inline constexpr int SPLITTER_HANDLE_WIDTH = CONTENT_MARGIN;

		// The splitter GRIP (STYLE-04, WorkspaceSplitter). Six 2 px dots with a pixel between them, which is the shape
		// Fusion draws and the one the dark theme already looked right in -- reproduced here so the LIGHT theme can
		// have it too, and so it can be centred (the style's own sits a pixel right of centre).

		inline constexpr int GRIP_DOT_SIZE  = 2;
		inline constexpr int GRIP_DOT_GAP   = 1;
		inline constexpr int GRIP_DOT_COUNT = 6;

		// Lightness steps from the splitter background, applied away from whichever end of the scale the background is
		// nearer. The two grip values are Fusion's dark-theme rendering measured back into this form, so the dark theme
		// is unchanged and the light theme becomes its mirror; the hover step is a much smaller nudge of the same kind.

		inline constexpr int GRIP_MAIN_CONTRAST  = 73;
		inline constexpr int GRIP_BEVEL_CONTRAST = 45;
		inline constexpr int GRIP_HOVER_CONTRAST = 8;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The titled band across the top of a pane (STYLE-13, PaneHeader).
	//-----------------------------------------------------------------------------------------------------------------

	namespace pane
	{
		// There is deliberately NO height or padding constant here. The header draws itself as a tab through the
		// style's own CT_TabBarTab measurement (STYLE-13), so its size comes from the same place the editor pane's tab
		// strip gets its own -- a constant would be a second opinion, and the two would drift the moment a style
		// changed.

		// How much of the width the title may claim before it elides. Only ever consulted when the pane is dragged
		// narrow enough for the title not to fit.

		inline constexpr int HEADER_HORIZONTAL_PADDING = 8;

		// The rule closing the band. One device-independent pixel: a divider, not a border.

		inline constexpr int HEADER_RULE_HEIGHT = 1;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The tree view pane (TREE-01..08).
	//-----------------------------------------------------------------------------------------------------------------

	namespace tree
	{
		// Per-level indent. Qt's default (20) is generous for a tree whose labels are short keys and which will often
		// sit six or more levels deep in a 200 px pane.

		inline constexpr int INDENTATION = 14;

		// Type-glyph size. 16 matches the menu, so the tree reads as the same icon system.

		inline constexpr int ICON_SIZE = 16;

		// How many levels are open when a document loads, measured RELATIVE TO THE FILE NODE -- the same convention as
		// QTreeView::expandRecursively, which this feeds directly. 0 opens the file node alone, revealing the document's
		// top-level keys.
		//
		// Raising it is expensive in a way the number does not advertise: 1 opens EVERY top-level key, so a document
		// with one large array at the top would materialize all of its elements on load, which is exactly the cost
		// lazy population exists to avoid (TREE-08).

		inline constexpr int INITIAL_EXPAND_DEPTH = 0;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The editor pane and its views (EDITOR-01..05).
	//-----------------------------------------------------------------------------------------------------------------

	namespace editor
	{
		// Tab-strip glyph size. 16 matches the menu and the tree, so the whole icon system reads as one set.

		inline constexpr int TAB_ICON_SIZE = 16;

		// The four tab surfaces (STYLE-14), as lightness distances from the CONTENT surface (QPalette::Base), applied
		// AWAY from whichever end of the scale the content is nearer -- style/tone.hpp's rule, and the same one the
		// splitter grip and the Code View gutter follow.
		//
		// WHY DISTANCES AND NOT COLOURS. Expressed as eight hex values (four per theme) the relationship between them is
		// invisible, and the light theme drifted out of step with the dark one precisely because nothing said what the
		// numbers were supposed to mean. Expressed as distances from the content, ONE rule generates all eight, and it
		// is the rule that is reviewable: the selected tab stands furthest from the content, the unselected tabs sit
		// nearer it, and the whole strip recedes toward the content when the pane loses the keyboard.
		//
		// The four values are the DARK theme's existing rendered fills, measured back into this form -- so dark is
		// unchanged and light becomes its mirror. That is what fixes the reported defect: Fusion draws an unselected tab
		// DARKER than the selected one in both themes, which reads correctly against dark content and backwards against
		// white, where it left the unselected tabs visually heavier than the selected one.

		inline constexpr int TAB_SELECTED_FOCUSED_CONTRAST     = 33;
		inline constexpr int TAB_UNSELECTED_FOCUSED_CONTRAST   = 26;
		inline constexpr int TAB_SELECTED_UNFOCUSED_CONTRAST   = 16;
		inline constexpr int TAB_UNSELECTED_UNFOCUSED_CONTRAST = 10;

		static_assert
		(
			( TAB_SELECTED_FOCUSED_CONTRAST   > TAB_UNSELECTED_FOCUSED_CONTRAST ) &&
			( TAB_SELECTED_UNFOCUSED_CONTRAST > TAB_UNSELECTED_UNFOCUSED_CONTRAST ),
			"the selected tab must stand FURTHER from the content than the unselected ones, in both focus states"
		);

		static_assert
		(
			( TAB_SELECTED_FOCUSED_CONTRAST   > TAB_SELECTED_UNFOCUSED_CONTRAST ) &&
			( TAB_UNSELECTED_FOCUSED_CONTRAST > TAB_UNSELECTED_UNFOCUSED_CONTRAST ),
			"the strip must recede toward the content when the pane loses the keyboard (STYLE-14)"
		);

		// The keyboard-focus marker under the current tab. Drawn only while the TAB BAR itself holds focus, which is the
		// state a tab click now puts the user in -- it is the affordance that says the arrow keys move between tabs
		// rather than through the document (NAV-04).

		inline constexpr int TAB_FOCUS_MARKER_HEIGHT = 2;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Form View's two grids -- the object form (EDITOR-02) and the array table (EDITOR-03). Both are QTableViews
	// over the same delegate, so they share their metrics deliberately: a field and a cell must feel like the same
	// control, which is the whole point of the "form / table parity" rule.
	//-----------------------------------------------------------------------------------------------------------------

	namespace form
	{
		// Added to the font's line height to size a row. Enough air that a row is comfortably clickable without the
		// grid turning into a list of buttons.

		inline constexpr int ROW_VERTICAL_PADDING = 6;

		// Auto-sizing bounds applied ONCE when a node is presented, after which columns are the user's (they are
		// Interactive, and nothing re-sizes them again -- a value refresh must never flap the layout, EDITOR-03).
		//
		// The maximum matters more than it looks: without it a single long string value in the first row would size its
		// column to the whole pane width and push every other column out of sight.

		inline constexpr int MINIMUM_COLUMN_WIDTH = 64;
		inline constexpr int MAXIMUM_COLUMN_WIDTH = 320;

		// Extra width added to a contents-derived column so text does not sit flush against the grid line.

		inline constexpr int COLUMN_PADDING = 16;

		// How long a refused-commit explanation stays in the status bar (VAL-04). Long enough to read without hunting
		// for it, short enough not to outlive the correction it is prompting.

		inline constexpr int REFUSAL_MESSAGE_TIMEOUT = 4000;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Text View (EDITOR-06 / SET-06).
	//-----------------------------------------------------------------------------------------------------------------

	namespace text_view
	{
		// Tab width, in characters of the view's fixed-width font. Only ever seen when a rendered STRING VALUE contains
		// a tab; the renderer itself never emits one except as the TSV table style's column delimiter, where a wide
		// stop is exactly what is wanted. 4 is the common editor default and keeps a TSV rendering readable.

		inline constexpr int TAB_STOP_CHARACTERS = 4;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Code View (EDITOR-07 / EDITOR-09).
	//-----------------------------------------------------------------------------------------------------------------

	namespace code
	{
		// Air either side of the gutter's digits (LineNumberArea).

		inline constexpr int GUTTER_HORIZONTAL_PADDING = 8;

		// The gutter never narrows below this many digits, so a document that crosses a power of ten mid-edit does not
		// visibly shunt the text sideways. 3 covers the great majority of documents outright.

		inline constexpr int GUTTER_MINIMUM_DIGITS = 3;

		// How long the text may sit unvalidated while the user is mid-keystroke (EDITOR-07's live validation). Every
		// change re-parses the WHOLE document, so validating per keystroke is what would cost NFR-03 on a large file;
		// coalescing to one pass per pause costs nothing a user can perceive, since the error message is only useful
		// once they have stopped typing anyway.

		inline constexpr int VALIDATION_DEBOUNCE = 150;

		// Lightness steps from the editor's own background, applied away from whichever end of the scale it is nearer
		// -- the same rule the splitter grip follows (STYLE-04), so a "subtly contrasting column" is one measurement
		// rather than two hand-picked colours per theme.
		//
		// The gutter and the current-line bar are deliberately near-equal and small: both are read-only overlays that
		// must be findable without competing with the syntax colours they sit behind.

		inline constexpr int GUTTER_SURFACE_CONTRAST      = 6;
		inline constexpr int GUTTER_TEXT_CONTRAST         = 55;   // Digits: legible, but clearly not document text.
		inline constexpr int GUTTER_CURRENT_TEXT_CONTRAST = 110;  // The caret's line number, lifted out of the column.
		inline constexpr int CURRENT_LINE_CONTRAST        = 8;

		// How long a commit / discard / refusal notice stays in the status bar (VAL-04). Matches the Form View's
		// refusal timeout -- they are the same kind of message and should not linger for different lengths of time.

		inline constexpr int MESSAGE_TIMEOUT = 4000;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Menu metrics, applied by FluentStyle.
	//
	// Measured Fusion baselines (Qt 6.10.1): separator 13, item row 21, both margins 0. The values are TUNED BY EYE
	// and are intentionally not derived from one another.
	//
	// SEPARATOR_HEIGHT is applied ABSOLUTELY, so it may be set above or below Fusion's 13 and the menu will follow it
	// either way. The others are floors or additions and therefore only take effect ABOVE their baseline -- setting
	// ITEM_MINIMUM_HEIGHT to 20, say, would be silently inert, which tst_fluent_style asserts against.
	//-----------------------------------------------------------------------------------------------------------------

	namespace menu
	{
		inline constexpr int SEPARATOR_HEIGHT    = 10;   // Absolute. Fusion's own is 13; tune freely either side.
		inline constexpr int ITEM_MINIMUM_HEIGHT = 25;   // Floor. Fusion gives 21; a restrained lift, not a full row.
		inline constexpr int VERTICAL_MARGIN     = 6;    // Fusion gives 0: no padding at all above/below.
		inline constexpr int HORIZONTAL_MARGIN   = 2;    // Fusion gives 0; insets the selection highlight.

		// The guard rail behind "separation comes from group breaks, not row padding": how much taller than the base
		// style a row may get before it is padding rather than lifting.

		inline constexpr int MAXIMUM_ROW_LIFT = 6;

		// A separator still has to draw a rule with air around it. Below this it collapses into the adjacent rows.

		inline constexpr int MINIMUM_SEPARATOR_HEIGHT = 5;

		static_assert
		(
			SEPARATOR_HEIGHT >= MINIMUM_SEPARATOR_HEIGHT,
			"config::menu::SEPARATOR_HEIGHT is too small for the rule to read as a group break"
		);
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Toolbar metrics.
	//-----------------------------------------------------------------------------------------------------------------

	namespace toolbar
	{
		// Separates the three command groups. Fusion gives 6.

		inline constexpr int SEPARATOR_EXTENT = 11;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Icon rendering.
	//-----------------------------------------------------------------------------------------------------------------

	namespace icons
	{
		// The pixel sizes each glyph is rasterized at. 16 and 24 are the logical menu and toolbar sizes; 32, 48, and
		// 64 exist so a 1.5x / 2x / 2.5x display finds an exact-size render rather than upscaling a smaller one.

		inline constexpr int RENDER_SIZES [] = { 16, 20, 24, 32, 48, 64 };
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Bounded lists.
	//-----------------------------------------------------------------------------------------------------------------

	namespace limits
	{
		// How many recent files to remember (FILE-05).

		inline constexpr int RECENT_FILES = 10;
	}
}

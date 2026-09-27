//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   AppConfig holds application settings that can be used by a developer to tune vje_app at compile time.
//
//   These are values a maintainer might reasonably want to adjust while working on the source, for example, layout 
//   metrics, look and feel, first-run defaults, and bounded limits. Most are not user-facing. A few (the first-run 
//   window size, the recent-files limit) are the built-in defaults behind settings the user can later override at 
//   run time through the SettingsStore, which always wins once a persisted value exists.
//
//   WHAT BELONGS HERE
//
//     Cross-cutting numbers that are otherwise scattered across widget construction code and easy to miss, example,
//     pane widths, margins, style metrics, render ladders, list caps.
//
//   WHAT DELIBERATELY DOES NOT
//
//     - Anything in vje_core. The core is UI-free and headlessly testable; a shared config header spanning both 
//       layers would breach that boundary. JsonParser::MAX_DEPTH stays with the parser.
//
//     - Format and protocol contracts, which are not tunables. SettingsStore::SCHEMA_VERSION is the settings-file
//       compatibility contract; changing it is a migration, not a preference.
//
//     - Values with only one meaningful call site and no cross-cutting significance -- resource path prefixes, the
//       command-line argument spellings, the icon geometry table (which lives with its generator).
//
//   NOTE:
//
//     - This header is included widely, so editing it rebuilds most of vje_app. That is an accepted trade we make for
//     the sake of having all application settings and default values be easily discoverable.
//
//     - It is also why the scope above is drawn narrowly. A dumping ground would both slow builds and separate 
//       constants from the code that gives them meaning.
//
//     - Sizes are logical pixels; Qt 6 scales them for high-DPI displays automatically, so no manual DPI factor 
//       applies.
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

		// -- The title bar's punctuation (spec section 2.2) -----------------------------------------------------------
		//
		// "VJE <separator> <document name> <marker when modified>". Both are UTF-8 byte strings rather than QStrings
		// because AppConfig is a header of compile-time values with no Qt dependency, and MainWindow wraps them once.
		//
		// The separator is U+25AA BLACK SMALL SQUARE and the marker U+25CF BLACK CIRCLE, each with its own spacing built
		// in so the two are adjustable independently -- a heavier glyph than the asterisk this replaced, and it needs
		// the air. The marker is NOT Qt's "[*]" placeholder; MainWindow::update_title says why that is forced.

		inline constexpr const char* TITLE_SEPARATOR      = " ▪ ";
		inline constexpr const char* TITLE_MODIFIED_MARKER = " ●";
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

		// -- The two splitter thicknesses ---------------------------------------------------------------------------
		//
		// One dial per splitter, because the two sit in different places and answer to different neighbours. Each is
		// the HANDLE's thickness across the split -- the width of the vertical bar, the height of the horizontal one --
		// and each is what the corresponding splitter passes to QSplitter::setHandleWidth.
		//
		// MASTER-DETAIL is the gap BETWEEN THE TWO CARDS, and STYLE-04 makes that gap itself the separator rather than
		// drawing a bar inside it. Its default is DERIVED from CONTENT_MARGIN rather than chosen, because STYLE-03 asks
		// for consistent 4 px-grid spacing and the two are the same frame: the gap between the cards and the gap
		// around them read as one measurement. Moving this off CONTENT_MARGIN is allowed and is a real choice -- it
		// makes the inter-card gap differ from the margin framing the pair, which is the thing that derivation existed
		// to prevent -- so change it deliberately and look at the whole workspace afterwards, not just the gutter.

		inline constexpr int MASTER_DETAIL_SPLITTER_THICKNESS = CONTENT_MARGIN;

		// The splitter GRIP (STYLE-04, WorkspaceSplitter). Six 2 px dots with a pixel between them, which is the shape
		// Fusion draws and the one the dark theme already looked right in -- reproduced here so the LIGHT theme can
		// have it too, and so it can be centred (the style's own sits a pixel right of centre).

		inline constexpr int GRIP_DOT_SIZE  = 2;
		inline constexpr int GRIP_DOT_GAP   = 1;
		inline constexpr int GRIP_DOT_COUNT = 6;

		// Lightness steps from the splitter background, applied away from whichever end of the scale the background is
		// nearer. The two grip values are Fusion's dark-theme rendering measured back into this form, so the dark theme
		// is unchanged and the light theme becomes its mirror; the hover step is a much smaller nudge of the same kind.

		// How far one keyboard nudge moves the splitter (NAV-06). Four steps of the 4 px layout grid: small enough to
		// settle on a width by holding the key, large enough that crossing the pane takes a moment rather than a minute.

		inline constexpr int KEYBOARD_RESIZE_STEP = 16;

		inline constexpr int GRIP_MAIN_CONTRAST  = 73;
		inline constexpr int GRIP_BEVEL_CONTRAST = 45;
		inline constexpr int GRIP_HOVER_CONTRAST = 8;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The workspace CARD surface (STYLE-01/02/05, views/Card).
	//-----------------------------------------------------------------------------------------------------------------

	//-----------------------------------------------------------------------------------------------------------------
	// Interface style (SET-12) -- the application's corner-radius discipline, and the ONE statement of it.
	//
	// STYLE-05 asks for two radii: ~8 px on container surfaces and ~4 px on controls. Both are the user's, and they are
	// one choice rather than two: a filleted card holding square buttons, or the reverse, is not a preference anyone
	// holds. So a single setting selects between the discipline (Fluent) and the base style's own shape (Classic), and
	// both halves -- views/Card and style/FluentStyle -- read this enum rather than a switch each.
	//
	// It supersedes SET-03's "Rounded pane corners" (2026-07-30 to 2026-08-03), which asked the same question of the
	// cards alone and left the controls with no way to answer it.
	//
	// WHAT IT DOES NOT GOVERN: colour, spacing, typography, iconography, and the menu metrics of config::menu, all of
	// which are identical under both values. This setting answers the question of SHAPE and nothing else, which is what
	// keeps it independent of Theme.
	//-----------------------------------------------------------------------------------------------------------------

	namespace appearance
	{
		enum class InterfaceStyle
		{
			Classic,   // The base style's own shape. Every override in FluentStyle's paint path is a plain delegation.
			Fluent     // STYLE-05's radius discipline: cards fillet where they fillet at all, controls round.
		};

		// Fluent is the default because it is the look STYLE-01..05 specify; Classic is the FALLBACK, and it is the
		// safe one to fall back to precisely because it is reached by not intervening at all.

		inline constexpr InterfaceStyle DEFAULT_INTERFACE_STYLE = InterfaceStyle::Fluent;

		// STYLE-05's control radius, half the container radius, on the same 4 px layout grid. Applied to the panels of
		// push buttons, tool buttons, line edits, combo boxes and spin boxes -- see style/FluentStyle for which
		// elements and why the list stops where it does.

		inline constexpr int CONTROL_CORNER_RADIUS = 4;

		// STYLE-05's CONTAINER radius, "~8 px", on the same grid: twice the control radius. Stated here, beside its pair,
		// since Phase 15k.3 gave it a second container to shape -- the Settings dialog's group boxes (SET-01c) as well as
		// the workspace cards. Each of those states its own radius in terms of this one (config::card, config::group_box),
		// so the two containers cannot drift apart, and each still decides WHICH of its corners take it: a card squares its
		// bottom corners for a stated reason, and a box has no such reason.

		inline constexpr int CONTAINER_CORNER_RADIUS = 8;

		// How far a DISABLED control's fill stands from its enabled one (STYLE-16). A style/tone distance, applied
		// away from whichever end of the lightness scale the enabled surface is nearer -- so one number is lighter on
		// the dark theme and darker on the light one, which is the requirement stated as arithmetic.
		//
		// 6 is HALF the distance from the edit-area colour to the surface the control sits on, which is the reference
		// worth knowing when tuning it: at 12 a disabled field lands exactly on the form's own background and reads as
		// having sunk into it, and at 6 it stops short -- still unmistakably a field, just a shade off white or a shade
		// off the dark base (light #FFFFFF -> #F9F9F9, dark #252526 -> #2A2A2B). Chosen deliberately quieter than the
		// landmark, since the greyed label is carrying half the message.
		//
		// It pairs with SET-01b's greyed LABEL and does not replace it: the label says which setting is unavailable,
		// the fill says the control is. Read-only controls are deliberately untouched -- see STYLE-16.

		inline constexpr int DISABLED_FILL_CONTRAST = 6;

		inline constexpr bool rounds_containers ( InterfaceStyle style )
		{
			return style == InterfaceStyle::Fluent;
		}

		inline constexpr bool rounds_controls ( InterfaceStyle style )
		{
			return style == InterfaceStyle::Fluent;
		}
	}

	namespace card
	{
		// The container corner radius (STYLE-02, "~8 px"; STYLE-05 makes it the radius every container surface uses),
		// stated once as config::appearance::CONTAINER_CORNER_RADIUS so the card and the Settings dialog's group boxes
		// share it. A multiple of the 4 px layout grid, like every other measurement in the frame.
		//
		// STATED PER EDGE, because the two edges answer to different things. The TOP of a card is chrome we draw --
		// the Explorer band, the tab strip -- and a fillet there is the card's own outline. The BOTTOM is where an
		// item view puts its horizontal SCROLL BAR, which is a full-width rectangular control the toolkit draws and we
		// do not: a fillet cuts its ends off on the diagonal, and the result reads as a rendering fault rather than as
		// a rounded corner. Squaring the bottom is therefore not a compromise on STYLE-02 -- it is the only shape that
		// lets the requirement hold for the corners the card actually owns.
		//
		// Set BOTTOM_CORNER_RADIUS to TOP_CORNER_RADIUS to get the uniform card back.
		//
		// THE TOP RADIUS IS THE USER'S (SET-12, "Interface style"). Filleted and square are both defensible looks and
		// the preference genuinely splits, so the choice is a setting rather than a number settled here; what this
		// constant fixes is the radius used WHEN that setting is Fluent. Under Classic the top corners square and
		// everything else about the card is unchanged.
		//
		// The DEFAULT is not stated here any more. It was, while "Rounded pane corners" was a toggle of its own; it now
		// lives with the setting that decides it, as config::appearance::DEFAULT_INTERFACE_STYLE, which the reader and
		// the schema both name for the same reason they both named the constant this replaced.

		inline constexpr int TOP_CORNER_RADIUS    = appearance::CONTAINER_CORNER_RADIUS;
		inline constexpr int BOTTOM_CORNER_RADIUS = 0;

		// One device-independent pixel: a border, not a bevel. Also the inset the content is laid out at, so the ring
		// frames the content rather than being painted across its outermost row.

		inline constexpr int BORDER_WIDTH = 1;

		// Lightness steps from the window BACKDROP, applied away from whichever end of the scale that backdrop is
		// nearer (style/tone.hpp), so one number serves both themes.
		//
		// Tuned by eye and deliberately small -- STYLE-02 asks for a "subtle" border, and the card is already told
		// apart from the backdrop by its surface. The test bounds it rather than pinning it: it must be distinguishable
		// from both the backdrop it closes against and the surface it encloses, which is what "subtle" cannot mean.

		inline constexpr int BORDER_CONTRAST = 24;
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

		// TREE-10 / SET-14: which rows carry the unsaved-change dot. The default is stated here once, and named by both
		// the schema and the reader in settings_profiles, so the pair tst_settings_schema's drift guard watches is one
		// constant.

		enum class ChangeMarkScope
		{
			ChangedNodesAndAncestors,                          // The changed node and every node above it.
			FileNodeOnly                                       // The file node alone, whenever anything changed.
		};

		inline constexpr ChangeMarkScope DEFAULT_CHANGE_MARK_SCOPE = ChangeMarkScope::ChangedNodesAndAncestors;

		// The dot itself (TREE-10): its diameter, and the gap between the end of the label and the dot. A dot rather
		// than a glyph, so it has no icon master and scales with nothing but these two numbers. Six pixels reads as a
		// mark beside 9-point text without competing with the 16 px type glyph at the row's other end.

		inline constexpr int CHANGE_MARK_DIAMETER = 6;
		inline constexpr int CHANGE_MARK_GAP      = 6;

		// SET-14a: the dot's colour by default, stated for the DARK theme -- which is how the setting is stored. The
		// Light theme shows it with its HSL lightness inverted (style/theme_colour), which for this mid-grey is #7F7F7F.
		// Named by the schema and by the reader in settings_profiles, so the drift guard watches one constant.

		inline constexpr const char* DEFAULT_CHANGE_MARK_COLOUR_DARK = "#808080";
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Tooltips (STYLE-17). One formatter reads these, so no call site wraps or truncates a tooltip of its own.
	//-----------------------------------------------------------------------------------------------------------------

	namespace tooltip
	{
		// The measure a tooltip wraps to, in characters. Qt shows a plain-text tooltip on one line however long it is,
		// so a long value would otherwise run to the edge of the screen; 72 is a comfortable reading measure.

		inline constexpr int LINE_LENGTH = 72;

		// The most lines a tooltip shows before it ends in an ellipsis. A tooltip is a glance, not a viewer: a value
		// longer than this is opened in the Form View or the Code View.

		inline constexpr int MAXIMUM_LINES = 12;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The editor pane and its views (EDITOR-01..05).
	//-----------------------------------------------------------------------------------------------------------------

	namespace editor
	{
		// Tab-strip glyph size. 16 matches the menu and the tree, so the whole icon system reads as one set.

		inline constexpr int TAB_ICON_SIZE = 16;

		// -- The registered views' identifiers, and which of them a first run opens (VIEW-03) ------------------------
		//
		// The ids are stated HERE rather than in each provider, and each provider's VIEW_ID is BUILT FROM the constant
		// beside it, so there is exactly one spelling of "form" in the codebase. The default open set names them, and
		// the two lists have no common ancestor otherwise: a typo in either would be a view that silently never opens
		// on a first run, with nothing to fail. Making the provider the reader removes the drift rather than testing
		// for it.
		//
		// The default is passed IN to EditorPane rather than read by it (MainWindow supplies it at composition), which
		// is the rule Card and ThemeService already follow for a setting: the pane owns the mechanism, AppConfig owns
		// the numbers, and the pane stays ignorant of which views exist.

		namespace view_ids
		{
			inline constexpr const char* FORM     = "form";
			inline constexpr const char* TEXT     = "text";
			inline constexpr const char* CODE     = "code";
			inline constexpr const char* JSONPATH = "jsonpath";
		}

		// Every view VJE 2.0 registers, so a first run is the tab strip as it was before the set became the user's. A
		// view added later is NOT opened by an existing user's stored set -- membership is membership, and it is the
		// only reading under which "I closed everything" survives a restart (VIEW-03, lesson D8).

		// The JSONPath view is deliberately NOT here (QUERY-01). VIEW-03 gives an existing user's stored open set no
		// view it did not already have, so opening the query view by default would make a fresh installation differ
		// permanently from every upgrade -- and the View menu is the one route to it either way. A view whose default
		// only ever applies to people who have never run the application is not really a default.

		inline constexpr const char* const DEFAULT_OPEN_VIEWS [] =
		{
			view_ids::FORM,
			view_ids::TEXT,
			view_ids::CODE
		};

		// -- The JSONPath Query view (QUERY-01) ----------------------------------------------------------------------
		//
		// The splitter's opening split, as stretch factors rather than pixels: the results get the room and the query
		// box gets enough to show a query broken across two or three lines. Stretch rather than sizes because the pane
		// is any width and any height, and a pixel split would be wrong at both extremes.

		inline constexpr int QUERY_RESULTS_STRETCH = 4;
		inline constexpr int QUERY_INPUT_STRETCH   = 1;

		// The query splitter's thickness -- the HEIGHT of its horizontal handle, the second of the two splitter dials
		// (the first is config::workspace::MASTER_DETAIL_SPLITTER_THICKNESS).
		//
		// SEEDED FROM THE MASTER-DETAIL ONE so the two match out of the box, which is what makes the query splitter
		// read as the same control as the workspace's rather than as a second kind of divider that happens to be
		// nearby. They are nonetheless INDEPENDENT: this one sits INSIDE the editor card, between two content widgets,
		// where the other sits BETWEEN two cards against the window backdrop -- so a value that is right in one place
		// is not automatically right in the other, and departing from parity here is a legitimate choice rather than a
		// drift. Change this alone to make the query gutter thicker or thinner than the workspace's.

		inline constexpr int QUERY_SPLITTER_THICKNESS = workspace::MASTER_DETAIL_SPLITTER_THICKNESS;

		// How much of a scalar's value a result row previews before eliding. A result row exists to be RECOGNIZED and
		// clicked, not read: the node itself is one click away in the other three views.

		inline constexpr int QUERY_PREVIEW_MAXIMUM_CHARACTERS = 200;

		// -- The workspace chrome shading dials (STYLE-11 / 13 / 14, revised at the 2026-07-24 review) ---------------
		//
		// Every value is a lightness DISTANCE from the content surface (QPalette::Base), applied away from whichever
		// end of the scale the content is nearer (style/tone.hpp) -- so a larger number stands further out from the
		// content: lighter on the dark theme, darker on the light one. These are deliberately the HAND-TUNING DIALS
		// for the tab strip and the Explorer band: adjust here, rebuild (build.bat), look. The tests pin the RELATIONS
		// between them (the selected tab stays clear of its field in both focus states; the band recedes on focus
		// loss; the light field matches the receded band), never the absolute values, so tuning does not fight the
		// suite.
		//
		// THE RULE. The UNSELECTED tabs are a STATIC FIELD -- one shade per theme, unmoved by focus -- and only the
		// SELECTED tab answers the keyboard: furthest out while its pane holds it, dropping to a middle shade (still
		// clear of the field) when it does not. That keeps "which view am I on?" answerable in BOTH focus states. The
		// two themes are tuned separately -- the dark strip wants a quieter field than the light one -- which is why
		// there are two value sets rather than the single mirrored set the strip previously used.

		// The Explorer band (STYLE-13): one strong / receded pair, the same in both themes, DECOUPLED from the
		// selected tab's surface -- which is what lets the light strip's field match the receded band exactly while
		// the unfocused active tab sits a step darker than both.

		inline constexpr int BAND_FOCUSED_CONTRAST   = 33;   // Dark ~#474747 / light #DEDEDE.
		inline constexpr int BAND_UNFOCUSED_CONTRAST = 16;   // Dark ~#363636 / light #EFEFEF.

		// The tab strip, DARK theme (content #252526).

		inline constexpr int DARK_TAB_SELECTED_FOCUSED_CONTRAST   = 33;   // ~#474747 -- the strip's strongest shade.
		inline constexpr int DARK_TAB_SELECTED_UNFOCUSED_CONTRAST = 16;   // ~#363636 -- the middle shade.
		inline constexpr int DARK_TAB_UNSELECTED_CONTRAST         = 10;   // ~#303030 -- the static field.

		// The tab strip, LIGHT theme (content #FFFFFF). The field is DERIVED from the receded band rather than
		// chosen: "the unselected tabs match the Explorer band's receded shade" is the stated rule of the 2026-07-24
		// review, so one number carries it and re-tuning the band moves the field with it. Give the field its own
		// number only to break that tie deliberately.

		inline constexpr int LIGHT_TAB_SELECTED_FOCUSED_CONTRAST   = 33;                        // #DEDEDE.
		inline constexpr int LIGHT_TAB_SELECTED_UNFOCUSED_CONTRAST = 26;                        // #E5E5E5 -- the middle shade.
		inline constexpr int LIGHT_TAB_UNSELECTED_CONTRAST         = BAND_UNFOCUSED_CONTRAST;   // #EFEFEF -- the static field.

		static_assert
		(
			( DARK_TAB_SELECTED_FOCUSED_CONTRAST > DARK_TAB_SELECTED_UNFOCUSED_CONTRAST ) &&
			( DARK_TAB_SELECTED_UNFOCUSED_CONTRAST > DARK_TAB_UNSELECTED_CONTRAST ),
			"the dark selected tab must recede on focus loss yet stay clear of the field, or the active view becomes unfindable"
		);

		static_assert
		(
			( LIGHT_TAB_SELECTED_FOCUSED_CONTRAST > LIGHT_TAB_SELECTED_UNFOCUSED_CONTRAST ) &&
			( LIGHT_TAB_SELECTED_UNFOCUSED_CONTRAST > LIGHT_TAB_UNSELECTED_CONTRAST ),
			"the light selected tab must recede on focus loss yet stay clear of the field, or the active view becomes unfindable"
		);

		static_assert
		(
			BAND_FOCUSED_CONTRAST > BAND_UNFOCUSED_CONTRAST,
			"the Explorer band must recede when the tree loses the keyboard (STYLE-14)"
		);

		// The keyboard-focus marker under the current tab. Drawn only while the TAB BAR itself holds focus, which is the
		// state a tab click now puts the user in -- it is the affordance that says the arrow keys move between tabs
		// rather than through the document (NAV-04).

		inline constexpr int TAB_FOCUS_MARKER_HEIGHT = 2;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Form View's two grids -- the object form (EDITOR-02) and the array table (EDITOR-03). Both are QTableViews
	// over the same delegate, and most of their metrics are shared deliberately: a field and a cell must feel like the
	// same control, which is the whole point of the "form / table parity" rule. ROW HEIGHT is the one metric that is
	// dialled per face -- see the two constants below for what that buys and what it costs.
	//-----------------------------------------------------------------------------------------------------------------

	namespace form
	{
		// THE ROW-HEIGHT DIALS, ONE PER GRID FACE. A row is `measured font height + the face's padding` -- enough air
		// that a row is comfortably clickable without the grid turning into a list of buttons. Measured rather than
		// stated in pixels, so a row tracks the font and the display scaling. Raise either to open that face's rows up.
		//
		// THERE ARE TWO OF THEM, AND THAT IS A DEPARTURE (2026-08-27). One dial governed both faces until now, on the
		// "form / table parity" reasoning in the banner above, and the comment here said in as many words that a
		// second dial would let the two grids drift apart. It would -- but the two faces are READ differently: an
		// object form is a column of labelled fields, while an array table is a spreadsheet, and how many rows fit
		// on screen at once is much of what one is for. Tightening the table's rows without also tightening the
		// form's is a legitimate thing to want, and one dial could not express it. The cost is exactly what parity
		// was protecting: set to different values the two grids stop feeling like one control, and nothing checks
		// that they do.
		//
		// Both are applied in ONE place, FormView::apply_row_height, which is also where the floor below is lifted.
		//
		//   OBJECT_ROW_VERTICAL_PADDING   The object form's fields (EDITOR-02). Also what JsonCellDelegate::sizeHint
		//                                 rebuilds a WRAPPED row's height from -- deliberately the same formula, so
		//                                 the gap between any two fields is identical whether either of them wrapped
		//                                 (Phase 11.7's third review round had to fix exactly that). The delegate may
		//                                 name the OBJECT dial specifically because only the object form ever wraps:
		//                                 the table's delegate is never told to (SET-05, apply_string_settings).
		//
		//   ARRAY_ROW_VERTICAL_PADDING    The array table's rows (EDITOR-03), which are uniform and Fixed whatever
		//                                 the wrap setting says -- "a row is a row" is the property a spreadsheet is
		//                                 read for, and uniform heights are also the virtualization half of handling
		//                                 a large array (architecture.md section 10).
		//
		// A FLOOR SITS UNDER BOTH DIALS AND IT IS QT'S RATHER THAN OURS (measured 2026-08-22, lesson Q54). A
		// QHeaderView clamps a section to its minimumSectionSize, and an unset minimum is derived from the style and
		// the font (17-20 px here), so a smaller setDefaultSectionSize is silently ignored. apply_row_height sets that
		// minimum to the formula's own answer for both faces, which lifts the floor -- so the ARRAY dial is honest at
		// any value.
		//
		// The OBJECT dial is honest down to about 5 and no further, for a second floor we do not own: while Wrap
		// strings is ON the object form's rows are ResizeToContents, and a HIDDEN QHeaderView still reports its own
		// section size hint (~18 px for that form's invisible row header), which no delegate hint can go under. Below
		// about 5 the wrapped rows stop shrinking while the unwrapped ones keep going, and the two stop agreeing by a
		// pixel or two. Recorded here rather than worked around; wrapping is off by default, so the visible effect is
		// small. The array table never wraps, which is why that floor reaches only one of the two dials.

		inline constexpr int OBJECT_ROW_VERTICAL_PADDING = 6;
		inline constexpr int ARRAY_ROW_VERTICAL_PADDING  = 6;

		// Auto-sizing bounds applied ONCE when a node is presented, after which columns are the user's (they are
		// Interactive, and nothing re-sizes them again -- a value refresh must never flap the layout, EDITOR-03).
		//
		// The maximum matters more than it looks: without it a single long string value in the first row would size its
		// column to the whole pane width and push every other column out of sight.

		inline constexpr int MINIMUM_COLUMN_WIDTH = 64;
		inline constexpr int MAXIMUM_COLUMN_WIDTH = 320;


		// Extra width added to a contents-derived column so text does not sit flush against the grid line.

		inline constexpr int COLUMN_PADDING = 16;

		// A wrapped row's HEIGHT is (lines x font height) + OBJECT_ROW_VERTICAL_PADDING -- the same formula an unwrapped row
		// uses, so the gap between any two fields is identical whether either of them wrapped. Taking the style's own
		// measured height instead leaves a wrapped block a pixel or two tighter against the field below it: invisible
		// alone, obvious in a column where every other gap is the other value (JsonCellDelegate::sizeHint).
		//
		// There is deliberately NO cap on the line count and NO sampling bound, so neither appears here as a dial. A row
		// cap was tried, to stop a pathological value producing a row taller than the viewport; per-pixel vertical
		// scrolling makes such a row ordinary to scroll through, and a cap hides the end of a value the user asked to
		// see. A WRAPPED_ROW_SAMPLE fed QHeaderView::setResizeContentsPrecision, which on a VERTICAL header caps the
		// COLUMNS sampled per row rather than the rows measured -- so it read as protection while doing nothing
		// (2026-07-28 review). What bounds the measurement is stated where it happens, in FormView::configure_wrapping.

		// How long a refused-commit explanation stays in the status bar (VAL-04). Long enough to read without hunting
		// for it, short enough not to outlive the correction it is prompting.

		inline constexpr int REFUSAL_MESSAGE_TIMEOUT = 4000;

		// EDITOR-17's index-column floor. The width is MEASURED -- horizontalAdvance ( INDEX_COLUMN_DIGIT_SAMPLE ) in
		// the table's own font -- rather than stated in pixels, so it tracks the font and the display scaling; what is
		// dialled here is the sample and the air around it. Two digits, because an array of fewer than ten elements
		// otherwise sizes the column to a single one, which was a fiddly target even before EDITOR-16 made it a click
		// target that matters.
		//
		// It is a FLOOR: a thousand-element array still widens to fit four digits.

		inline constexpr const char* INDEX_COLUMN_DIGIT_SAMPLE = "00";
		inline constexpr int         INDEX_COLUMN_PADDING      = 14;

		// EDIT-15's sort control. A column header is divided into TWO ZONES: a wide one that selects the column
		// (EDITOR-16) and a narrow one at its right that sorts. The zone is the click target and the triangle is only
		// its mark -- aiming at a 9 x 5 triangle was a fiddly target, and every near miss selected the column instead
		// of sorting it, which is the opposite command.
		//
		// SORT_ZONE_WIDTH is therefore a HIT TARGET rather than an ornament: it spans the section's full height, and
		// GridHeaderView::sort_zone_rect computes it ONCE for both the paint and the hit test, so "a click outside the
		// zone selects" stays a property of one rectangle rather than an agreement between two.

		inline constexpr int SORT_ZONE_WIDTH    = 24;
		inline constexpr int SORT_MARKER_WIDTH  = 9;
		inline constexpr int SORT_MARKER_HEIGHT = 5;

		// The label needs room beside the zone, or a column would be all control and no name. A section narrower than
		// the zone plus this carries no zone at all -- and therefore no sort target, which is what keeps a narrow
		// column selectable.

		inline constexpr int SORT_ZONE_MINIMUM_LABEL_WIDTH = 12;

		// How far the divider between the two zones is toned from the header's own surface. Present so the boundary
		// can be SEEN: a target the user has to discover by trial is barely better than the triangle it replaced.

		inline constexpr int SORT_ZONE_DIVIDER_CONTRAST = 22;

		// How far the marker's colour is toned from the header's text colour when it is merely AVAILABLE rather than
		// active. An always-full-strength triangle in every column reads as several controls asking to be pressed;
		// the active column's marker is drawn at full strength (style/tone's distance, as everywhere else).

		inline constexpr int SORT_MARKER_IDLE_CONTRAST = 34;

		// EDITOR-16: how strongly a SELECTED header section is tinted with the palette's accent. A tint rather than a
		// fill, deliberately: a header section filled with the accent would put its label on top of the accent, and
		// Phase 15 accepted that sub-AA contrast exception only on the condition that nothing new comes to depend on
		// reading text against it. A tint moves the ground slightly, so the label keeps the contrast it had.
		//
		// The definitive signal is the CELLS, which carry the ordinary selection highlight the grid already uses; the
		// section's job is only to say which header the selection came from.

		inline constexpr int SELECTED_SECTION_TINT_ALPHA = 64;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Text View has no dial of its own. It had a tab width until 2026-07-28, when a review established that a tab
	// must occupy exactly ONE column there: the renderer aligns separators, draws table rules and measures wrap widths
	// by counting characters, so any character rendering wider than one column breaks all three at once. That is a
	// correctness constraint rather than a preference, so it belongs beside the constraint in TextView's constructor
	// and not here as something to tune.
	//-----------------------------------------------------------------------------------------------------------------

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

		// The fold markers (EDITOR-23) sit in a column of their own between the digits and the text, one line height
		// wide so each marker is a square cell. The chevron spans this fraction of the cell and is stroked at this width
		// in logical pixels -- a stroke rather than a filled triangle, to read as the tree's own expanders do.

		inline constexpr double FOLD_CHEVRON_EXTENT = 0.45;
		inline constexpr double FOLD_CHEVRON_STROKE = 1.25;

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
	// Find and Go To (FIND-01..04).
	//-----------------------------------------------------------------------------------------------------------------

	namespace find
	{
		// How long a find report ("4 of 17 matches", "No matches") or a Go To confirmation stays in the status bar's
		// message area (VAL-04). Matched to the Form View's and the Code View's refusal timeout -- they are the same
		// kind of transient message and should not linger for different lengths of time. The find bar's own label is
		// NOT on a timer: it is the persistent copy, and the status bar carries the transient one (FIND-02).

		inline constexpr int MESSAGE_TIMEOUT = 4000;

		// The bar's frame and the gap between its controls. On the 4 px layout grid (STYLE-03), like the workspace's.

		inline constexpr int BAR_MARGIN  = 4;
		inline constexpr int BAR_SPACING = 4;

		// The query field. It stretches with the pane, so this is only the floor -- enough for a realistic needle to be
		// readable when the editor pane has been dragged narrow.

		inline constexpr int QUERY_FIELD_MINIMUM_WIDTH = 180;

		// The match-count label's floor. Reserved rather than fitted, because the label's text changes on every step and
		// a label that resized to its content would shunt the buttons sideways as the user held F3. Wide enough for the
		// longest ordinary report at the default font.

		inline constexpr int COUNT_LABEL_MINIMUM_WIDTH = 120;

		// The previous / next / close buttons. The glyph is menu-sized (16, matching the tree and the tab strip) and the
		// button square is a touch larger than the glyph so the three read as one control group rather than as toolbar
		// buttons that wandered into the workspace.

		inline constexpr int BUTTON_ICON_SIZE = 16;
		inline constexpr int BUTTON_SIZE      = 24;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The shared modal dialog structure (STYLE-15), and the ONE PLACE its numbers are stated: every modal VJE shows
	// takes them. The dialogs the application lays out itself (About, Settings, Go To, Import XML to JSON) through
	// dialogs/dialog_frame; the message boxes (dialogs/MessageBox) and the text prompts (dialogs/text_prompt), whose
	// layouts are Qt's, through dialog_frame's apply_button_rule. Only the platform's pickers are outside it.
	//
	// THE STRUCTURE, TOP TO BOTTOM: the content, inset by CONTENT_MARGIN; RULE_PADDING_ABOVE; a rule RULE_THICKNESS
	// high, edge to edge; RULE_PADDING_BELOW; the button row; CONTENT_MARGIN. What a particular dialog adds -- a size,
	// a column width -- is in that dialog's own group below, stated in terms of these where it means the same thing.
	//-----------------------------------------------------------------------------------------------------------------

	namespace dialog
	{
		// The margin framing the content and the button row, and the gap between rows inside a dialog's content. The
		// 4 px grid (STYLE-03), matching the values the Go To and XML import dialogs already used, so adopting the frame
		// moved nothing.

		inline constexpr int CONTENT_MARGIN = 12;
		inline constexpr int ROW_SPACING    = 8;

		// The rule closing the content, above the buttons. One logical pixel: it separates two regions rather than
		// framing either, and a heavier line reads as a border around the buttons.

		inline constexpr int RULE_THICKNESS = 1;

		// The space either side of the rule: above it, between the content and the line; below it, between the line
		// and the buttons. Stated separately from the margins they equal today, so the rule's air can be tuned without
		// moving the dialog's inset.

		inline constexpr int RULE_PADDING_ABOVE = CONTENT_MARGIN;
		inline constexpr int RULE_PADDING_BELOW = ROW_SPACING;

		// How far the rule stands from the dialog's own surface, and how far dimmed prose stands from it -- both as
		// style/tone DISTANCES, applied away from whichever end of the lightness scale the surface is nearer, so one
		// number covers both themes and cannot be silently inert on either (style/tone.hpp).
		//
		// DIMMED_PROSE_CONTRAST is bounded from BELOW by legibility rather than chosen for looks: explanatory prose is
		// read rather than glanced at, so it must clear WCAG AA against the surface it sits on. At 150 it measures
		// 5.9:1 on the light theme and 7.9:1 on the dark one, against ordinary text's 217 and 194 -- clearly quieter
		// than the label above it, and clearly not the accepted sub-AA exception QPalette::PlaceholderText carries
		// (spec section 5). tst_dialog_frame asserts the AA floor rather than the number.

		inline constexpr int RULE_CONTRAST         = 32;
		inline constexpr int DIMMED_PROSE_CONTRAST = 150;

		// The outline of a group box inside a dialog (SET-01c), as the same kind of distance. EQUAL to the rule's, so the
		// rule above the buttons and the boxes above it read as one line weight -- and stated separately, as the rule's
		// padding is, so the boxes can be tuned without moving the rule.

		inline constexpr int GROUP_FRAME_CONTRAST = RULE_CONTRAST;

		// The application icon shown beside the About dialog's text (HELP-04), and the gap between the two.
		//
		// 40 IS AN AUTHORED MASTER RATHER THAN A CHOSEN METRIC. The application icon is hand-drawn raster
		// (assets/images/icons/application-icon/), so it is sharp at the sizes it was drawn at and resampled at every
		// other one -- which is the whole reason it ships as a directory of masters rather than one file. Asking for a
		// size nobody drew would put a blurred logo in the one dialog whose job is to present the product.
		//
		// It is therefore NOT free to tune: changing it means drawing that master first. The guard rail cannot be a
		// static_assert here -- config::icons::DESIGN_SIZES governs the UI glyph set, which this icon is not part of
		// -- so tst_about_dialog asserts the delivered pixmap is exactly this size, which is what a missing master
		// would break (QIcon returns its largest entry below the request rather than upscaling).

		inline constexpr int ABOUT_ICON_SIZE    = 40;
		inline constexpr int ABOUT_ICON_SPACING = 16;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Go To dialog (FIND-04).
	//-----------------------------------------------------------------------------------------------------------------

	namespace go_to_dialog
	{
		// First-open width. The dialog is a single line edit and a message; the height is whatever the layout asks for,
		// so only the width is stated -- wide enough for a realistically deep pointer without horizontal scrolling.

		inline constexpr int DEFAULT_WIDTH = 460;

		// The margin and row gap that used to live here are gone (2026-08-06): the inset is stated once by
		// config::dialog and applied by dialogs/dialog_frame, so no dialog states its own any more.
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Message boxes (STYLE-18), applied by dialogs/MessageBox. The inset is config::dialog::CONTENT_MARGIN, so a message
	// box's buttons line up with the framed dialogs'; only the size floor is the message box's own.
	//-----------------------------------------------------------------------------------------------------------------

	namespace message_box
	{
		// The smallest a message box opens at, in logical pixels of its client area (the window manager's title bar is
		// not included). A FLOOR, never a fixed size: a longer message grows the box past it, and past Qt's own width
		// cap the text wraps. Measured before it was chosen (Qt 6.10.1, Windows): the application's shortest box opened
		// at 166 x 100 unfloored, so the floor is live for every short message.

		inline constexpr int MINIMUM_WIDTH  = 300;   // 400 for part of 2026-09-26, then back to 300; the height was kept.
		inline constexpr int MINIMUM_HEIGHT = 150;

		// The icon a message box carries, in logical pixels, answered by FluentStyle for every box. 32 is Windows' own
		// message-box icon size (LD-9); Fusion's default was 48 (measured), which read as oversized beside one line.

		inline constexpr int ICON_SIZE = 32;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// One-line text prompts (dialogs/text_prompt): the key the Add commands ask for, and Rename Key.
	//-----------------------------------------------------------------------------------------------------------------

	namespace text_prompt
	{
		// The width a prompt opens at, in logical pixels. Qt's own opened at 200 (measured), narrower than most keys a
		// user renames; the height stays the layout's.

		inline constexpr int WIDTH = 300;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Group boxes (SET-01c), drawn by dialogs/GroupBox: a one-pixel outline with its title set into the top edge -- the
	// classic group box, as the Settings dialog divides its pages. The line through the title sits at the title's
	// vertical centre; everything below is measured from that line, not from the top of the title.
	//-----------------------------------------------------------------------------------------------------------------

	namespace group_box
	{
		// The outline. One logical pixel, like the rule above a dialog's buttons, which it is drawn to match.

		inline constexpr int FRAME_THICKNESS = dialog::RULE_THICKNESS;

		// Outline to content, the same on all four sides: the dialog inset, so a box holds its rows the way the dialog
		// holds its content. At the top it is measured from the line the title sits on.

		inline constexpr int PADDING = dialog::CONTENT_MARGIN;

		// Where the title's TEXT starts, from the box's left edge: the content inset, so the title lines up with the
		// labels beneath it and the box reads as one column.

		inline constexpr int TITLE_INSET = PADDING;

		// The clear space either side of the title, where the outline stops short of it.

		inline constexpr int TITLE_GAP = 4;

		// The corner radius WHEN corners are rounded, which is SET-12's choice (Fluent rounds, Classic squares): STYLE-05's
		// container radius, since a box is a container.

		inline constexpr int CORNER_RADIUS = appearance::CONTAINER_CORNER_RADIUS;

		// The break in the outline must start where the corner's arc has finished -- a gap cut into the arc would leave
		// the corner as a hook rather than a curve.

		static_assert
		(
			( TITLE_INSET - TITLE_GAP ) >= CORNER_RADIUS,
			"the title's gap must begin after the corner's arc ends"
		);
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Title bars (STYLE-19), coloured by services/TitleBarSync where the platform allows it (Windows 11).
	//-----------------------------------------------------------------------------------------------------------------

	namespace title_bar
	{
		// How far below the window surface the title bar sits, in lightness steps, on BOTH themes (style/tone's
		// darker_tone). 15 is Windows' own dark step, measured (Windows 11 build 26200): its dark caption #202020 at
		// lightness 32 against VJE's dark surface #2D2D30 at 47 -- QColor's lightness, which rounds #2D2D30's 46.5 up;
		// the first reading of 14 took it as 46, and tst_title_bar caught the dark caption a step too light. So the dark
		// theme looks as it did before VJE coloured anything, and the light theme gets the same step -- #F3F3F3 to
		// #E4E4E4 -- where Windows' own light caption was #F3F3F3 exactly, and there was no step at all.

		inline constexpr int DARKER_STEP = 15;
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

	//-----------------------------------------------------------------------------------------------------------------
	// The Document menu's edit commands (EDIT-02..13), and what they say about how an edit went (VAL-04).
	//-----------------------------------------------------------------------------------------------------------------

	namespace edit
	{
		// How long an edit command's outcome stays in the status bar. Matches the find bar, the Code View and the
		// Form View's refusal message, which is the point rather than a coincidence: a user who has just pressed a
		// command should not have to learn a different dwell time per surface.

		inline constexpr int MESSAGE_TIMEOUT = 4000;
	}

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
		// Separates the command groups. Fusion gives 6.

		inline constexpr int SEPARATOR_EXTENT = 9;

		// Toolbar glyph size, in logical pixels -- the hand-tuning dial behind QToolBar::setIconSize (2026-07-24;
		// previously the style default, which resolved to the same 16). The button square follows automatically: the
		// style pads the icon by a few pixels per side, so raising this to 24 grows the buttons with it.
		//
		// It MUST be one of icons::GRIDS (16 or 20) -- those are the two sizes the icon set is actually DRAWN for, and
		// anything else is a master rendered at a size it was not authored on. The guard rail at the foot of this file
		// enforces it; it exists because this constant spent a release at 18, where the blur read as a poor asset
		// rather than as a wrong number. Raising it to 24 is therefore no longer a one-line change: it would need a
		// 24-unit master DRAWN first, which is a set of 43 glyphs rather than a constant.
		//
		// 20 is one rung above the menu, the tree (tree::ICON_SIZE), and the tab strip (editor::TAB_ICON_SIZE), which
		// is the deliberate divergence: the toolbar's glyphs carry the command on their own, with no label beside them.

		inline constexpr int ICON_SIZE = 20;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Icon rendering.
	//-----------------------------------------------------------------------------------------------------------------

	namespace icons
	{
		// THE SET IS TWO FAMILIES, and Settings > Appearance > Interface style picks one (SET-12 / SET-13).
		//
		//   Classic -- VJE's own artwork, hand-drawn RASTER, at assets/images/icons/classic/png/<deviceSize>/
		//   Fluent  -- a VECTOR set, at assets/images/icons/fluent/{microsoft,custom}/<designSize>/
		//
		// Raster for one and vector for the other is a CONSEQUENCE rather than a second choice: hand-drawn artwork is
		// authored as pixels, and Microsoft ships SVG. Everything below follows from it, including the one asymmetry
		// worth naming out loud -- Classic's directory number is a DEVICE size and Fluent's is a DESIGN size. Classic
		// has seven drawings and serves a display scaling by picking one; Fluent has two designs and serves it by
		// rendering.

		//-------------------------------------------------------------------------------------------------------------
		// The two LOGICAL sizes the application asks for -- 16 for the tree, the view tabs, the find bar and the
		// transfer list, 20 for the toolbar -- and therefore the two sizes the Fluent set has to be DESIGNED at.
		//
		// A vector glyph is designed per size rather than scaled: at 16 it carries less detail than at 24, which is why
		// Microsoft ships one file per size and why picking the nearest design is the right rule rather than rendering
		// one drawing at everything. The guard rail at the foot of this file asserts every icon consumer asks for one
		// of these two.
		//-------------------------------------------------------------------------------------------------------------

		inline constexpr int DESIGN_SIZES [] = { 16, 20 };

		//-------------------------------------------------------------------------------------------------------------
		// THE DEVICE-PIXEL LADDER, STATED ONCE.
		//
		// Each rung is a DEVICE pixel size and the design that serves it. The seven device sizes are the two logical
		// sizes at the four Windows display scalings -- 100 / 125 / 150 / 175 % -- and they are exactly the seven
		// directories the Classic tree is drawn at, which is not a coincidence: tools/seed_icon_masters.cpp's
		// DEFAULT_PLAN is this table minus the two identity rungs.
		//
		// IT IS A TABLE AND NOT A PRODUCT LOOP, and that is load-bearing. 16 x 1.25 and 20 x 1.0 both come to 20 device
		// pixels, so iterating designs x ratios would visit 20 twice -- and under Fluent the second visit would
		// silently overwrite the first with the OTHER design's artwork. Naming the design per rung makes that
		// unrepresentable, and the uniqueness assert at the foot of this file keeps it so.
		//
		// WHAT THIS CLOSES, AND WHAT IT DOES NOT. QIcon matches by device size, so registering every rung means every
		// one of those eight (family, logical size, scaling) combinations is served by an EXACT render rather than a
		// scaled neighbour -- which is the fractional-DPR gap lessons-learned Q28 records. At a device pixel ratio of
		// 2.0 or above (32 / 40 device pixels and up) both families fall off the ladder again and QIcon scales. That is
		// strictly better than scaling a 16 to 32, and it is not the same as solved: closing it for Classic means
		// drawing two more masters per glyph, and for Fluent it means a QIconEngine rendering at the requested size.
		//-------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------
		// CLASSIC IS DRAWN PER SIZE, one master per rung of the ladder below, and each surface reads the file for the
		// size it asks for: the toolbar 20, the tree and the menus 16, and the fractional rungs at higher display
		// scalings. That is what the ladder is for, and it is what keeps a glyph crisp at the size it is used at
		// rather than resampled from a neighbour.
		//
		// THE COST IS STATED SO IT IS NOT REDISCOVERED: it is seven drawings per glyph, so a glyph is only coloured
		// where it has been coloured. Serving every rung from one master was tried on 2026-08-12 and reverted the
		// same day -- it made a half-drawn set look finished, which is the opposite of useful while the set is being
		// drawn. tools/seed_icon_masters.cpp is what seeds a new size from an existing one to be corrected by hand.
		//-------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------
		// How strongly a DISABLED Classic glyph is drawn, as a fraction of full opacity (SET-13a).
		//
		// Classic is artwork, so its disabled form fades the drawing rather than draining it. The obvious alternative
		// -- QStyle::generatedIconPixmap, which is what the toolkit does unaided -- maps luminance through a
		// black-to-background-to-white ramp and STRIPS EVERY HUE: measured over the shipped masters, document-save
		// goes from 65 coloured pixels to 0, vje-object from 107 to 0. That is conventional (Office and Explorer both
		// do it) and it is wrong for this family, because it makes a half-coloured toolbar read as half-broken and
		// throws away the identity Classic exists to carry.
		//
		// Fading instead means we state the disabled appearance rather than delegating it, which is a cost worth
		// naming: this constant IS that statement. 0.40 is Fluent's own disabled weight, and it is a dial -- low
		// enough to read as unavailable beside a full-strength neighbour, high enough that the glyph is still
		// identifiable rather than a smudge.
		//
		// Fluent needs none of this: its glyphs paint in one substitutable colour, so a disabled one is simply drawn
		// in the palette's disabled text colour.
		//-------------------------------------------------------------------------------------------------------------

		inline constexpr double DISABLED_ARTWORK_OPACITY = 0.40;

		struct Rung
		{
			int deviceSize;   // What the display actually asks for: logical size x device pixel ratio.
			int designSize;   // Which design answers it. Classic reads its own drawing at deviceSize.
		};

		inline constexpr Rung LADDER [] =
		{
			{ 16, 16 },   // 16 @ 100 %
			{ 20, 20 },   // 20 @ 100 %, and 16 @ 125 % -- one rung, because the 20 drawing answers both exactly.
			{ 24, 16 },   // 16 @ 150 %
			{ 25, 20 },   // 20 @ 125 %
			{ 28, 16 },   // 16 @ 175 %
			{ 30, 20 },   // 20 @ 150 %
			{ 35, 20 }    // 20 @ 175 %
		};
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The master-detail Settings dialog (section 2.10, SET-01).
	//-----------------------------------------------------------------------------------------------------------------

	namespace settings_dialog
	{
		// The master list's width. Wide enough for the longest group name ("Form View Editor") without the list
		// dominating the dialog; the detail pane takes the rest.

		inline constexpr int MASTER_PANE_WIDTH = 180;

		// First-open size. The dialog is resizable and Qt remembers nothing about it between openings, so this is what it
		// opens at every time. Sized by the Toolbar group (SET-04) since 2026-07-27: its transfer list is a page rather
		// than a column of rows, and wants both lists showing a useful number of commands without scrolling. The other
		// groups were previously the constraint (Text View's six rows) and now sit comfortably inside it -- still so
		// since SET-01c (Phase 15k.3) put every page's rows in group boxes, each adding a title line and its padding;
		// tst_settings_dialog fails if the tallest page ever needs more than this.

		inline constexpr int DEFAULT_WIDTH  = 760;
		inline constexpr int DEFAULT_HEIGHT = 500;

		// The gap between the label column and the editor column, and the margin framing the detail page -- the dialog
		// inset, stated by config::dialog, so the page sits where every other dialog's content does.

		inline constexpr int COLUMN_SPACING = dialog::CONTENT_MARGIN;
		inline constexpr int PAGE_MARGIN    = dialog::CONTENT_MARGIN;

		// The gap between two rows of a page, and between two group boxes (SET-01c). The rows were at the style's own
		// default spacing until Phase 15k.3; they are now the dialog's row gap, stated. Boxes are further apart than rows,
		// by the dialog inset, so a box's edge never reads as belonging to the row beside it.

		inline constexpr int ROW_SPACING     = dialog::ROW_SPACING;
		inline constexpr int SECTION_SPACING = dialog::CONTENT_MARGIN;

		//-------------------------------------------------------------------------------------------------------------
		// The transfer list (SET-04, section 2.10). A page-spanning composite: two lists, a column of transfer buttons
		// between them, reorder buttons under the left list, and Restore Defaults under the right.
		//-------------------------------------------------------------------------------------------------------------

		namespace transfer_list
		{
			// Minimum height of each list. Below roughly this the lists show too few commands to arrange a toolbar in,
			// and the page starts to feel like a scroll box rather than a workbench.

			inline constexpr int LIST_MINIMUM_HEIGHT = 260;

			// The column between the two lists. Wide enough for the two arrow buttons plus the gutters either side.

			inline constexpr int BUTTON_COLUMN_WIDTH = 44;

			// The square transfer / reorder buttons. Matched to the toolbar's own glyph size so the page's controls sit
			// in the same size register as the buttons they arrange.

			inline constexpr int BUTTON_SIZE = 30;

			// Row glyph size. The lists show each command's toolbar icon, and 16 keeps them at menu size rather than
			// letting a list row grow to toolbar height.

			inline constexpr int ROW_ICON_SIZE = 16;

			// Half the base of the transfer / reorder arrows, which are PAINTED rather than typed (see ArrowButton in
			// TransferListEditor.cpp). All four are the same triangle rotated, so one number sizes the set and they
			// cannot drift apart the way four font glyphs did.
			//
			// The triangle is base 2x this by this tall, centred -- so at 4 it occupies 9 x 5 logical pixels inside a
			// BUTTON_SIZE square, which reads as an arrow without competing with the list text beside it.

			inline constexpr int ARROW_HALF_EXTENT = 4;

			static_assert
			(
				( ARROW_HALF_EXTENT * 2 ) < BUTTON_SIZE,
				"the transfer arrow must fit inside its button with room for the button's own chrome"
			);

			// The gap between the two lists' columns, and between a list and the controls beneath it -- the dialog's row
			// gap, stated by config::dialog.

			inline constexpr int COLUMN_GAP  = dialog::ROW_SPACING;
			inline constexpr int CONTROL_GAP = dialog::ROW_SPACING;
		}

		//-------------------------------------------------------------------------------------------------------------
		// WHICH SETTINGS THE DIALOG OFFERS.
		//
		// One switch per group and one per individual setting, all on by default. Turning one off removes that row (or
		// that whole master-list entry) from the dialog and nothing else: the setting keeps its stored value, its
		// documented default, and every reader that consults it. This is the developer's dial for hiding a setting that
		// is not ready, not wanted in a build, or superseded -- not a user-facing feature.
		//
		// Turning a group off hides its settings whatever their own switches say. A group whose every setting is off is
		// dropped as well, so the master list never offers an empty page.
		//
		// The Toolbar group's CONTENT is the window's own command catalogue (SET-04), and the group is ONE transfer-list
		// field over it -- so it has a group switch and no per-setting ones. There is no fixed list of buttons here to
		// switch, and switching the single field off would leave an empty page, which is what the group switch is for.
		//-------------------------------------------------------------------------------------------------------------

		namespace show
		{
			// -- Groups (SET-02).

			inline constexpr bool GENERAL_GROUP            = true;
			inline constexpr bool APPEARANCE_GROUP         = true;
			inline constexpr bool TOOLBAR_GROUP            = true;
			inline constexpr bool FORM_VIEW_GROUP          = true;
			inline constexpr bool TEXT_VIEW_GROUP          = true;
			inline constexpr bool CODE_EDITOR_GROUP        = true;
			inline constexpr bool PRINTING_GROUP           = true;
			inline constexpr bool SYSTEM_GROUP             = true;

			// -- General (SET-03).

			inline constexpr bool CHECK_UPDATES            = true;
			inline constexpr bool ON_DUPLICATE_KEYS        = true;
			inline constexpr bool ALLOW_DUPLICATE_KEYS     = true;

			// -- Appearance (SET-11). Theme and String display moved here from General on 2026-08-03; their stored
			//    keys did not move with them, so these switches changed group and nothing else.

			inline constexpr bool THEME                    = true;
			inline constexpr bool INTERFACE_STYLE          = true;
			inline constexpr bool STRING_DISPLAY           = true;
			inline constexpr bool MARK_UNSAVED_CHANGES     = true;     // SET-14, the Explorer box.
			inline constexpr bool CHANGE_MARK_COLOUR       = true;     // SET-14a, beside it.

			// -- Form View Editor (SET-05).

			inline constexpr bool FORM_EDIT_ON             = true;
			inline constexpr bool FORM_ALLOW_JAGGED_PASTE  = true;
			inline constexpr bool FORM_ALLOW_KEY_EDITING   = true;
			inline constexpr bool FORM_WRAP_STRINGS        = true;

			// -- Text View (SET-06).

			inline constexpr bool TEXT_WRAP_STRINGS        = true;
			inline constexpr bool TEXT_BLANK_LINES         = true;
			inline constexpr bool TEXT_ALIGN_SEPARATORS    = true;
			inline constexpr bool TEXT_NAME_SEPARATOR      = true;
			inline constexpr bool TEXT_INCLUDE_OBJECTS     = true;
			inline constexpr bool TEXT_INCLUDE_ARRAYS      = true;
			inline constexpr bool TEXT_MARKDOWN_STYLE      = true;
			inline constexpr bool TEXT_TABLE_STYLE         = true;

			// -- Code Editor (SET-07). The first four are the document format profile, shared with File > Save.

			inline constexpr bool CODE_INDENT_KIND         = true;
			inline constexpr bool CODE_INDENT_SIZE         = true;
			inline constexpr bool CODE_SYNTAX_HIGHLIGHTING = true;
			inline constexpr bool CODE_BRACE_STYLE         = true;
			inline constexpr bool CODE_ALIGN_SEPARATORS    = true;
			inline constexpr bool CODE_EDIT_ON             = true;

			// -- Printing (SET-10, FILE-12).

			inline constexpr bool PRINT_PAGE_RULES         = true;

			// -- System (SET-09). The folder and file name are inert while logging is off, so hiding the toggle alone
			//    would leave two editors nothing can enable -- hide the group instead.

			inline constexpr bool DIAGNOSTIC_LOGGING       = true;
			inline constexpr bool LOG_FOLDER               = true;
			inline constexpr bool LOG_FILE_NAME            = true;

			// -- System (SET-15). The build's switch only: the row also needs a platform that supports the feature,
			//    which settings_schema asks the platform layer, so on Linux it is absent whatever this says.

			inline constexpr bool EXPLORER_INTEGRATION     = true;

			// -- Debug settings.
			//    These should generaly only be made visible during development and debugging, and should be turned
			//    off for production builds. They are not typical user facing features.
			//
			//    The GROUP switch is here rather than up with the others deliberately: the group is developer tooling
			//    rather than one of SET-02's user-facing groups, so keeping its flag beside its field flags makes the
			//    block one contiguous edit.
			//
			//    DEBUG_GROUP off hides all of it whatever the individual switches say -- so that one line is the
			//    production build's off switch, and the rest narrow what is on screen during a session.
			//
			//    ON now, because the icon-authoring loop uses the one setting below: a hand-drawn PNG master is checked
			//    by switching the source to Png and looking at the running application. Set DEBUG_GROUP to false for a
			//    release build.

		}
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The Import XML to JSON dialog (FILE-13, section 2.11).
	//-----------------------------------------------------------------------------------------------------------------

	namespace xml_import
	{
		// First-open size. Wider and taller than the other dialogs because the preview is the point of it: a JSON
		// rendering read at a fixed-width font needs the width, and judging a strategy needs more than a few lines.

		inline constexpr int DEFAULT_WIDTH  = 900;
		inline constexpr int DEFAULT_HEIGHT = 640;

		// The margin and row gap that used to live here are gone (2026-08-06): the inset and the rhythm are stated once
		// by config::dialog, so the four dialogs cannot sit at three different insets.

		// How the options column and the preview divide the width. The options are a fixed-content column and the
		// preview takes the rest, so these are stretch factors rather than pixels.

		inline constexpr int OPTIONS_STRETCH = 2;
		inline constexpr int PREVIEW_STRETCH = 3;

		// The strategy list's floor. Four ONE-line rows, so it does not open needing to be scrolled -- the descriptions
		// moved out of the rows and into their own block beneath the list on 2026-08-06 (section 2.11), which is why
		// this is roughly half what it was.

		inline constexpr int STRATEGY_LIST_MINIMUM_HEIGHT = 96;

		// The description block's floor, in the same column. Reserved rather than left to the text, so arrowing down
		// the list does not shuffle the controls beneath it as a one-line description follows a two-line one -- two
		// lines is the longest of the four (section 2.11) at the width the options column opens at.

		inline constexpr int DESCRIPTION_MINIMUM_HEIGHT = 44;

		// How much of the conversion the preview shows. Section 2.11 permits a truncated preview on a large file, and
		// this is where that is decided: past this many lines the rendering is cut and the dialog says so. It bounds the
		// text handed to QPlainTextEdit and its highlighter, which is the cost that actually grows with the file -- the
		// conversion itself is a tree walk.

		inline constexpr int PREVIEW_MAXIMUM_LINES = 2000;

		// A coalescing delay for a preview the user is TYPING at (Custom flattened's text value key). A strategy click
		// or a toggle is one event and re-renders immediately; a key field is one event per keystroke, and re-converting
		// the whole file on each of them is the one way this dialog can feel slow (NFR-03).

		inline constexpr int PREVIEW_TYPING_DELAY = 250;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Printing (FILE-12).
	//
	// These are POINT sizes rather than pixels, and deliberately so: everything else in this header is a logical pixel
	// on a screen whose resolution Qt already knows, while a printed character has a physical size that must not follow
	// the printer's dots per inch. A 9 pt body is 9 pt on a 600 dpi laser and on a PDF alike.
	//-----------------------------------------------------------------------------------------------------------------

	namespace printing
	{
		// The proportional body: the Form View's printed tables. Small, because a form printed at screen size wastes
		// most of a page.

		inline constexpr int BODY_POINT_SIZE = 9;

		// Preformatted content -- the Text View's renderings and the Code View's JSON. A point smaller than the body,
		// because a fixed-width character is wider than a proportional one at the same size and these are the two
		// renderings whose lines can be long (a Spreadsheet-style table, a deeply indented array).

		inline constexpr int FIXED_POINT_SIZE = 8;

		// The page header and footer. Smaller again, so the furniture reads as furniture.

		inline constexpr int FURNITURE_POINT_SIZE = 7;

		// How tall each furniture band is, in lines of the furniture font: one line of text plus one of air, so the body
		// never sits hard against the page number. Expressed in LINES rather than points so it tracks the size above.

		inline constexpr int FURNITURE_BAND_LINES = 2;

		// The printed table's grid. Cell padding is in pixels of the printer's own resolution as Qt's rich text reads
		// it, so it scales with the device rather than being a fixed physical measure.

		inline constexpr int TABLE_BORDER_WIDTH = 1;
		inline constexpr int TABLE_CELL_PADDING = 3;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Bounded lists.
	//-----------------------------------------------------------------------------------------------------------------

	namespace limits
	{
		// How many recent files to remember (FILE-05).

		inline constexpr int RECENT_FILES = 10;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Guard rails.
	//
	// EVERY LOGICAL ICON SIZE IN THE APPLICATION MUST BE ONE THE SET IS DESIGNED FOR. A size outside icons::DESIGN_SIZES
	// has no artwork drawn for it in either family, so IconLibrary could only hand QIcon a render of a design meant for
	// some other size -- crisp at best where the size happens to be an integer multiple, silently scaled and soft
	// otherwise, and soft in a way that reads as a poor asset rather than as a wrong number.
	//
	// This is a STRONGER rule than the ladder check it replaced. Under the old single 24-unit master, "on the ladder"
	// only meant "no resampling"; every rung was still a fractional multiple of the source grid and therefore soft.
	// config::toolbar::ICON_SIZE spent a release at 18, was moved to 20 to get onto that ladder, and was STILL soft --
	// the ladder was never the property worth checking. Asking for a designed size is.
	//
	// Checking it here rather than in a test is deliberate -- it is a property of the CONSTANT, it costs nothing, and it
	// fails at the edit rather than at the next test run. Add a line below whenever a new surface starts asking
	// IconLibrary for a size; adding a THIRD logical size means drawing a third Classic master at every rung it
	// introduces AND a third Fluent design, which is exactly the cost this assert exists to make visible at the edit.
	//
	// The two ladder asserts underneath are the same idea one level down: they are what stops icons::LADDER from naming
	// a design nobody drew, or from listing one device size twice -- the second being the failure the table exists to
	// prevent (see LADDER's comment).
	//-----------------------------------------------------------------------------------------------------------------

	namespace icons
	{
		constexpr bool is_design_size ( int pixelSize )
		{
			for ( const int candidate : DESIGN_SIZES )
			{
				if ( candidate == pixelSize )
				{
					return true;
				}
			}

			return false;
		}

		constexpr bool ladder_designs_are_all_drawn ()
		{
			for ( const Rung& rung : LADDER )
			{
				if ( !is_design_size ( rung.designSize ) )
				{
					return false;
				}
			}

			return true;
		}

		constexpr bool ladder_device_sizes_are_unique ()
		{
			for ( const Rung& rung : LADDER )
			{
				int occurrences = 0;

				for ( const Rung& other : LADDER )
				{
					if ( other.deviceSize == rung.deviceSize )
					{
						++occurrences;
					}
				}

				if ( occurrences != 1 )
				{
					return false;
				}
			}

			return true;
		}
	}

	static_assert ( icons::ladder_designs_are_all_drawn (),
	                "a rung of icons::LADDER names a design size nobody drew -- see icons::DESIGN_SIZES" );

	static_assert ( icons::ladder_device_sizes_are_unique (),
	                "icons::LADDER lists a device size twice -- the second rung would silently overwrite the first with another design's artwork" );

	static_assert ( icons::is_design_size ( toolbar::ICON_SIZE ),
	                "config::toolbar::ICON_SIZE is not one of icons::DESIGN_SIZES -- the toolbar glyphs would be scaled and soft" );

	static_assert ( icons::is_design_size ( tree::ICON_SIZE ),
	                "config::tree::ICON_SIZE is not one of icons::DESIGN_SIZES -- the tree glyphs would be scaled and soft" );

	static_assert ( icons::is_design_size ( editor::TAB_ICON_SIZE ),
	                "config::editor::TAB_ICON_SIZE is not one of icons::DESIGN_SIZES -- the view tabs would be scaled and soft" );

	static_assert ( icons::is_design_size ( find::BUTTON_ICON_SIZE ),
	                "config::find::BUTTON_ICON_SIZE is not one of icons::DESIGN_SIZES -- the find bar's buttons would be scaled and soft" );

	static_assert ( icons::is_design_size ( settings_dialog::transfer_list::ROW_ICON_SIZE ),
	                "config::settings_dialog::transfer_list::ROW_ICON_SIZE is not one of icons::DESIGN_SIZES -- the list rows would be scaled and soft" );
}

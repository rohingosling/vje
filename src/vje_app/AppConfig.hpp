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

	namespace card
	{
		// The container corner radius (STYLE-02, "~8 px"; STYLE-05 makes it the radius every container surface uses).
		// A multiple of the 4 px layout grid, like every other measurement in the frame.
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
		// THE TOP RADIUS IS THE USER'S (SET-03, "Rounded pane corners"). Filleted and square are both defensible looks
		// and the preference genuinely splits, so the choice is a setting rather than a number settled here; what this
		// constant fixes is the radius used WHEN the setting is on. Switching it off squares the top corners and leaves
		// everything else about the card alone.

		inline constexpr int TOP_CORNER_RADIUS    = 8;
		inline constexpr int BOTTOM_CORNER_RADIUS = 0;

		// The first-run value behind SET-03's toggle, and the ONLY statement of that default: settings_profiles reads
		// with it and the Settings schema offers it, so the two cannot drift and the drift guard has nothing to catch
		// here. (Most defaults in this application are stated twice by necessity -- this one need not be, because both
		// statements can name the same constant.)

		inline constexpr bool ROUNDED_TOP_CORNERS_DEFAULT = true;

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
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The editor pane and its views (EDITOR-01..05).
	//-----------------------------------------------------------------------------------------------------------------

	namespace editor
	{
		// Tab-strip glyph size. 16 matches the menu and the tree, so the whole icon system reads as one set.

		inline constexpr int TAB_ICON_SIZE = 16;

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

		// A wrapped row's HEIGHT is (lines x font height) + ROW_VERTICAL_PADDING -- the same formula an unwrapped row
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
	// The Go To dialog (FIND-04).
	//-----------------------------------------------------------------------------------------------------------------

	namespace go_to_dialog
	{
		// First-open width. The dialog is a single line edit and a message; the height is whatever the layout asks for,
		// so only the width is stated -- wide enough for a realistically deep pointer without horizontal scrolling.

		inline constexpr int DEFAULT_WIDTH = 460;

		// The margin framing the dialog's contents and the gap between its rows. The 4 px grid again (STYLE-03).

		inline constexpr int CONTENT_MARGIN = 12;
		inline constexpr int ROW_SPACING    = 8;
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
		// The icon set ships as TWO MASTERS, one per logical size the application asks for:
		// assets/images/icons/svg/16 and .../svg/20. They are separate artwork rather than one drawing scaled, because
		// SVG has no hinting -- a stroke is crisp only when it is a whole number of device pixels wide AND centred on a
		// half-integer device coordinate, and the generator can only state both at once by authoring a grid whose unit
		// IS a pixel at its own size.
		//
		// Crispness then survives every INTEGER multiple of the grid and nothing in between: the 16 master is exact at
		// 16 / 32 / 48 / 64 and soft at 20, the 20 master is exact at 20 / 40 / 60 / 80 and soft at 16. That is the
		// whole reason there are two.

		inline constexpr int GRIDS [] = { 16, 20 };

		// Each master is rasterized at these multiples of its own grid, which is what covers device pixel ratios 1x
		// through 4x with an exact-size render rather than an upscale of a smaller one.

		inline constexpr int SCALE_MULTIPLES [] = { 1, 2, 3, 4 };

		//-------------------------------------------------------------------------------------------------------------
		// Which rasterization source IconLibrary serves the set from.
		//
		// The set is committed in BOTH forms and both are compiled in (src/vje_app/CMakeLists.txt): the PNG masters
		// under assets/images/icons/png/<grid>/<size>/ and the SVG set under assets/images/icons/svg/<grid>/. Which of
		// the two is the artwork, and which is generated from it, is ARTWORK_SOURCE below -- this constant is only
		// about which one is READ. Switching is therefore this one constant and a rebuild -- no CMake edit, no asset
		// regeneration -- which is the whole point: it is a FALLBACK, and a fallback that needs a build-system change
		// to reach is not one.
		//
		// The two are interchangeable rather than merely similar, because both sources are tinted from the palette at
		// load time -- the PNGs are alpha masks whose baked colour the application discards -- and because whichever
		// tree is DERIVED is produced through the same QSvgRenderer pass at the same sizes IconLibrary would have used.
		// So the choice does not change what is on screen, only when the rasterization happened. Spec section 2.9's
		// "icons recolour with the theme" holds either way.
		//
		// SINCE THE SVG SET BECAME A TRANSCRIPTION OF THE PNG ONE (2026-07-31, see ARTWORK_SOURCE below) the two are
		// pixel-identical at every authored size rather than merely shape-identical, which removes the one reason this
		// constant used to change what the user sees. What remains is a payload and reach difference, and it runs one
		// way: SVG is 25 KB against the PNG tree's 47 KB for the base rungs ALONE -- restoring the full 1x-4x ladder
		// would take the raster tree past 180 KB while the vector one does not move -- and SVG is the only form that
		// can be rendered at a size nobody exported, including the fractional device pixel ratios a QIconEngine would
		// need (see the guard rail at the foot of this file). PNG earns its keep as the fallback for a platform where
		// QSvgRenderer is unavailable or misbehaving; it needs no Qt SVG module at load time, though the module stays a
		// hard build dependency either way.
		//-------------------------------------------------------------------------------------------------------------

		enum class SourceFormat
		{
			Svg,   // Rasterize the vector set at load time.
			Png    // Load the pre-rasterized masks and tint them.
		};

		inline constexpr SourceFormat SOURCE_FORMAT = SourceFormat::Svg;

		// Where a rasterized pixel stops counting as inked, wherever an antialiased coverage map has to be cut into the
		// two-value set the PNG tree commits (2026-07-31).
		//
		// THE PNG SET IS DELIBERATELY ALIASED so the files can be hand-edited pixel by pixel; an antialiased edge has
		// no single pixel to select, fill or erase. That is now a property of the artwork itself rather than of a
		// conversion -- the masters are drawn two-valued -- so the threshold has retreated to two narrower jobs: it is
		// the cut tools/export_icon_pngs makes when the arrow runs the other way (ArtworkSource::Vector), and it is
		// what tools/trace_png_to_svg.py resolves a stray antialiased pixel with rather than dropping it silently.
		//
		// 96 is measured. At the midpoint 128 a 1.0-wide stroke laid around an arc -- spread over two pixel columns at
		// roughly half coverage each -- loses BOTH columns and the ring breaks into dashes; edit-find, help-about and
		// vje-null all fail that way, and disintegrate at 160. At 96 every stroke in all 43 glyphs survives at both
		// authored sizes.
		//
		// It lives here because tst_icon_library needs it under ArtworkSource::Vector, where the two trees agree only
		// once the reference render is cut at the same place. Under Raster it is NOT applied to the comparison, and
		// deliberately: a transcription is exact, so hardening the reference there would let a genuinely soft render
		// pass by rounding it to the answer expected. tools/export_icon_pngs and tools/trace_png_to_svg.py each restate
		// the value, being standalone tools that cannot include this header -- the arrangement SCALE_MULTIPLES has.

		inline constexpr int PNG_ALPHA_THRESHOLD = 96;

		//-------------------------------------------------------------------------------------------------------------
		// WHICH OF THE TWO TREES IS THE ARTWORK (2026-07-31). The other is derived from it, and drift between them is
		// an error either way -- this says which direction to regenerate in, and how exactly they are obliged to agree.
		//
		// Vector -- the SVG masters are drawn (historically by tools/generate_icons.py, as strokes on a half-integer
		//           grid) and tools/export_icon_pngs rasterize them into the PNG tree. The two agree only after the
		//           antialiased render is cut at PNG_ALPHA_THRESHOLD, so the comparison is made on the cut.
		//
		// Raster -- the PNG masters are drawn or corrected by hand, pixel by pixel, and tools/trace_png_to_svg.py
		//           transcribes them into the SVG tree. The two agree EXACTLY: a binary bitmap is a polygon set whose
		//           vertices are all integers, so tracing its boundaries and rasterizing them back reproduces it pixel
		//           for pixel at every integer multiple of the grid, with no antialiasing anywhere to cut.
		//
		// Raster is what ships. It reverses an arrow that pointed the other way for the whole of Phases 5 to 14, and
		// the reason is that the stroke-based masters could not be corrected: nudging a glyph meant editing geometry
		// in a generator and re-rendering to find out what it did, where a two-valued PNG can simply be drawn. The
		// direction that survived is the one a human can actually author in.
		//
		// The exactness is not a nicety -- it is what lets SOURCE_FORMAT stay a free choice. While the trees merely
		// resembled one another, switching source changed what was on screen; now it does not.
		//
		// EACH GENERATOR REBUILDS ITS TARGET TREE WHOLESALE, which is why this is stated rather than inferred: running
		// the wrong one replaces the artwork with a re-derivation of a derivation. tools/export_icon_pngs additionally
		// refuses a non-empty target without --overwrite, since under Raster it is the one that would destroy work.
		//-------------------------------------------------------------------------------------------------------------

		enum class ArtworkSource
		{
			Vector,   // The SVG masters are the artwork; the PNGs are rasterized from them (compared on the cut).
			Raster    // The PNG masters are the artwork; the SVGs are traced from them (compared exactly).
		};

		inline constexpr ArtworkSource ARTWORK_SOURCE = ArtworkSource::Raster;
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
		// groups were previously the constraint (Text View's six rows) and now sit comfortably inside it.

		inline constexpr int DEFAULT_WIDTH  = 760;
		inline constexpr int DEFAULT_HEIGHT = 500;

		// The gap between the label column and the editor column, and the margin framing the detail page. Multiples of
		// the 4 px layout grid (STYLE-03), like the workspace's own.

		inline constexpr int COLUMN_SPACING = 12;
		inline constexpr int PAGE_MARGIN    = 12;

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

			// The gap between the two lists' columns, and between a list and the controls beneath it. Both on the 4 px
			// grid (STYLE-03).

			inline constexpr int COLUMN_GAP = 8;
			inline constexpr int CONTROL_GAP = 8;
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
			inline constexpr bool TOOLBAR_GROUP            = true;
			inline constexpr bool FORM_VIEW_GROUP          = true;
			inline constexpr bool TEXT_VIEW_GROUP          = true;
			inline constexpr bool CODE_EDITOR_GROUP        = true;
			inline constexpr bool PRINTING_GROUP           = true;
			inline constexpr bool SYSTEM_GROUP             = true;

			// -- General (SET-03).

			inline constexpr bool THEME                    = true;
			inline constexpr bool CHECK_UPDATES            = true;
			inline constexpr bool ON_DUPLICATE_KEYS        = true;
			inline constexpr bool STRING_DISPLAY           = true;
			inline constexpr bool ROUNDED_PANE_CORNERS     = true;

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

		// The margin framing the dialog's contents and the gap between its rows. The 4 px grid (STYLE-03), matching the
		// Go To dialog so the two do not sit at different insets.

		inline constexpr int CONTENT_MARGIN = 12;
		inline constexpr int ROW_SPACING    = 8;

		// How the options column and the preview divide the width. The options are a fixed-content column and the
		// preview takes the rest, so these are stretch factors rather than pixels.

		inline constexpr int OPTIONS_STRETCH = 2;
		inline constexpr int PREVIEW_STRETCH = 3;

		// The strategy list's floor. Four two-line rows, so it does not open needing to be scrolled.

		inline constexpr int STRATEGY_LIST_MINIMUM_HEIGHT = 150;

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
	// EVERY LOGICAL ICON SIZE IN THE APPLICATION MUST HAVE A MASTER AUTHORED FOR IT. A size that is not one of
	// icons::GRIDS has no artwork drawn on its pixel grid, so IconLibrary can only hand QIcon a render of a master
	// drawn for some other size -- crisp at best if the size happens to be an integer multiple, silently scaled and
	// soft otherwise, and soft in a way that reads as a poor asset rather than as a wrong number.
	//
	// This is a STRONGER rule than the ladder check it replaces. Under the old single 24-unit master, "on the ladder"
	// only meant "no resampling"; every rung was still a fractional multiple of the source grid and therefore soft.
	// config::toolbar::ICON_SIZE spent a release at 18, was moved to 20 to get onto that ladder, and was STILL soft --
	// the ladder was never the property worth checking. Asking for an authored grid is.
	//
	// Checking it here rather than in a test is deliberate -- it is a property of the CONSTANT, it costs nothing, and it
	// fails at the edit rather than at the next test run. Add a line below whenever a new surface starts asking
	// IconLibrary for a size; adding a THIRD logical size means DRAWING a third master -- 43 more glyphs, on a third
	// grid -- which is exactly the cost this assert exists to make visible at the point of the edit.
	//
	// KNOWN LIMIT, stated so it is not mistaken for coverage: this checks LOGICAL sizes, and QIcon asks for logical x
	// device pixel ratio. Integer ratios are covered by icons::SCALE_MULTIPLES; a fractional one is not (20 x 1.25 = 25
	// is a multiple of neither grid). Closing that needs a QIconEngine rendering the SVG at the requested device size,
	// which IconLibrary deliberately does not have -- see its header, and Phase 15. That route exists only under
	// icons::SOURCE_FORMAT == Svg; a pre-rasterized set has nothing to re-render, which is the one capability the two
	// sources do NOT share now that they carry identical pixels.
	//-----------------------------------------------------------------------------------------------------------------

	namespace icons
	{
		constexpr bool is_authored_grid ( int pixelSize )
		{
			for ( const int candidate : GRIDS )
			{
				if ( candidate == pixelSize )
				{
					return true;
				}
			}

			return false;
		}
	}

	static_assert ( icons::is_authored_grid ( toolbar::ICON_SIZE ),
	                "config::toolbar::ICON_SIZE has no authored master in icons::GRIDS -- the toolbar glyphs would be scaled and soft" );

	static_assert ( icons::is_authored_grid ( tree::ICON_SIZE ),
	                "config::tree::ICON_SIZE has no authored master in icons::GRIDS -- the tree glyphs would be scaled and soft" );

	static_assert ( icons::is_authored_grid ( editor::TAB_ICON_SIZE ),
	                "config::editor::TAB_ICON_SIZE has no authored master in icons::GRIDS -- the view tabs would be scaled and soft" );

	static_assert ( icons::is_authored_grid ( find::BUTTON_ICON_SIZE ),
	                "config::find::BUTTON_ICON_SIZE has no authored master in icons::GRIDS -- the find bar's buttons would be scaled and soft" );

	static_assert ( icons::is_authored_grid ( settings_dialog::transfer_list::ROW_ICON_SIZE ),
	                "config::settings_dialog::transfer_list::ROW_ICON_SIZE has no authored master in icons::GRIDS -- the list rows would be scaled and soft" );
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   IconLibrary -- the bundled icon set shared by the menu, toolbar, and tree (TREE-03, STYLE-06, SET-13).
//
//   TWO FAMILIES, AND Settings > Appearance > Interface style PICKS ONE. They are different artwork rather than one
//   set with different corners, which is the whole of SET-13:
//
//     Classic  :/vje/icons/classic/<deviceSize>/<name>.png        VJE's own hand-drawn raster set
//     Fluent   :/vje/icons/fluent/microsoft/<designSize>/<name>.svg   vendored from Microsoft's Fluent System Icons
//              :/vje/icons/fluent/custom/<designSize>/<name>.svg      VJE glyphs Microsoft has no equivalent for
//
//   Raster for one and vector for the other is a CONSEQUENCE rather than a second choice: hand-drawn artwork is
//   authored as pixels and Microsoft ships SVG. It is also the reason the directory number means different things --
//   Classic's is a DEVICE size (one drawing per display scaling) and Fluent's is a DESIGN size (one drawing per
//   logical size, rendered at whatever is asked for). See config::icons::LADDER, which states both in one table.
//
//   MICROSOFT WINS AND CUSTOM IS THE FALLBACK BED, which is what makes the vendoring incremental: with the Microsoft
//   tree empty, Fluent is already a complete set served by the custom glyphs, and each vendored file replaces one
//   without anything else having to land with it. The precedence is a stated probe rather than an emergent property --
//   merging the two sub-trees into one resource prefix would resolve a collision by REGISTRATION ORDER, silently,
//   which is the wrong failure mode for exactly the overlap this design creates.
//
//   THE TWO FAMILIES ANSWER THE PALETTE DIFFERENTLY, and that follows from what each one IS (SET-13a).
//
//   Classic is ARTWORK: every master is drawn exactly as authored and the palette does not reach it. Many are still
//   monochrome black while the set is being coloured glyph by glyph, but that is a property of the drawing rather than
//   a mode this class detects -- there is one branch, not two, and no file is treated differently from its neighbour.
//   Its disabled form is the toolkit's, generated from the current palette.
//
//   Fluent is a MASK SET: every glyph paints in currentColor -- the custom ones by construction, Microsoft's because
//   the vendoring tool normalises them -- so the palette colour is substituted into the source before it is
//   rasterized, and the whole family recolours with the theme.
//
//   EVERY RUNG CARRIES BOTH MODES, in both families, and that is load-bearing rather than tidy. QIcon matches MODE
//   FIRST AND SIZE SECOND: an absent Disabled entry at one size does not fall back to that size's Normal pixmap, it
//   reaches for a Disabled entry at ANOTHER size and scales it. Filling every slot is what makes that unrepresentable
//   -- see add_raster_size, where it bit for real.
//
//   A MISSING ASSET IS SKIPPED, NEVER FATAL. That rule arrived in 2026-07-31 as a bug fix and is now load-bearing: a
//   partially-vendored Microsoft tree is the normal state for the length of this phase, and an undrawn ladder rung
//   costs that rung alone rather than the glyph.
//
//   Icons are cached per name and the cache is dropped whenever ThemeService reapplies a palette -- including the
//   OS-driven repaint under the System theme -- and whenever the family changes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "AppConfig.hpp"

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

class QColor;
class QImage;

namespace vje
{
	class ThemeService;

	//-----------------------------------------------------------------------------------------------------------------
	// Icon names. These are the resource base names under ":/vje/icons/". Standard freedesktop names are used wherever
	// one exists, so a future QIcon::fromTheme fallback to the desktop icon theme on Linux needs no renaming; concepts
	// with no freedesktop equivalent (the JSON type glyphs, the tree expand/collapse commands) take a "vje-" prefix.
	//-----------------------------------------------------------------------------------------------------------------

	namespace icon_names
	{
		// File.

		inline const QString DOCUMENT_NEW        = QStringLiteral ( "document-new" );
		inline const QString DOCUMENT_OPEN       = QStringLiteral ( "document-open" );
		inline const QString DOCUMENT_SAVE       = QStringLiteral ( "document-save" );
		inline const QString DOCUMENT_SAVE_AS    = QStringLiteral ( "document-save-as" );
		inline const QString DOCUMENT_CLOSE      = QStringLiteral ( "window-close" );
		inline const QString DOCUMENT_PRINT      = QStringLiteral ( "document-print" );
		inline const QString DOCUMENT_PAGE_SETUP = QStringLiteral ( "document-page-setup" );

		// Edit.

		inline const QString EDIT_UNDO  = QStringLiteral ( "edit-undo" );
		inline const QString EDIT_REDO  = QStringLiteral ( "edit-redo" );
		inline const QString EDIT_CUT   = QStringLiteral ( "edit-cut" );
		inline const QString EDIT_COPY  = QStringLiteral ( "edit-copy" );
		inline const QString EDIT_PASTE = QStringLiteral ( "edit-paste" );
		inline const QString EDIT_FIND  = QStringLiteral ( "edit-find" );
		inline const QString GO_TO      = QStringLiteral ( "go-jump" );

		// Copy JSON Pointer (FIND-05). Built on edit-copy's two sheets with go-jump's arrowhead in the front one, which
		// is the family rule vje-duplicate already set: two sheets plus a mark, the mark saying which copy this is.

		inline const QString COPY_POINTER = QStringLiteral ( "vje-copy-pointer" );

		// Node operations.

		inline const QString NODE_DELETE    = QStringLiteral ( "edit-delete" );
		inline const QString NODE_DUPLICATE = QStringLiteral ( "vje-duplicate" );
		inline const QString NODE_RENAME    = QStringLiteral ( "vje-rename" );
		inline const QString NODE_MOVE_UP   = QStringLiteral ( "go-up" );
		inline const QString NODE_MOVE_DOWN = QStringLiteral ( "go-down" );

		// Array transforms (EDIT-11..13). Added 2026-07-27: the toolbar is icon-only, so a glyph is what makes a
		// command toolbar-ELIGIBLE (SET-04's catalogue) -- these three were unadorned while they lived in menus alone.

		inline const QString NORMALIZE_ARRAY  = QStringLiteral ( "vje-normalize-array" );
		inline const QString ARRAY_TO_OBJECTS = QStringLiteral ( "vje-array-to-objects" );
		inline const QString OBJECTS_TO_ARRAY = QStringLiteral ( "vje-objects-to-array" );

		// View.

		inline const QString EXPAND_ALL   = QStringLiteral ( "vje-expand-all" );
		inline const QString COLLAPSE_ALL = QStringLiteral ( "vje-collapse-all" );

		// Preferences and help.

		inline const QString SETTINGS = QStringLiteral ( "preferences-system" );
		inline const QString ABOUT    = QStringLiteral ( "help-about" );

		// JSON type glyphs -- the tree's per-type icons (TREE-03). TYPE_DOCUMENT is the tree's ROOT node, which stands
		// for the file rather than for a JSON value and therefore keeps the document glyph whatever the root's type is.

		inline const QString TYPE_DOCUMENT = QStringLiteral ( "text-x-generic" );

		inline const QString TYPE_OBJECT  = QStringLiteral ( "vje-object" );
		inline const QString TYPE_ARRAY   = QStringLiteral ( "vje-array" );
		inline const QString TYPE_STRING  = QStringLiteral ( "vje-string" );
		inline const QString TYPE_NUMBER  = QStringLiteral ( "vje-number" );
		inline const QString TYPE_BOOLEAN = QStringLiteral ( "vje-boolean" );
		inline const QString TYPE_NULL    = QStringLiteral ( "vje-null" );

		// Document > Add -- the type glyph carrying a corner plus badge, so the add commands read as the same concept
		// the tree shows.

		inline const QString ADD_OBJECT  = QStringLiteral ( "vje-add-object" );
		inline const QString ADD_ARRAY   = QStringLiteral ( "vje-add-array" );
		inline const QString ADD_STRING  = QStringLiteral ( "vje-add-string" );
		inline const QString ADD_NUMBER  = QStringLiteral ( "vje-add-number" );
		inline const QString ADD_BOOLEAN = QStringLiteral ( "vje-add-boolean" );
		inline const QString ADD_NULL    = QStringLiteral ( "vje-add-null" );

		// Document > Convert To (EDIT-09). The SAME base glyph as the Add family at the same place on the grid, with the
		// corner plus badge replaced by a corner arrow -- so the two families read as "this type" plus a mark saying what
		// is being done with it, which is vje-copy-pointer's rule and vje-duplicate's before it.

		inline const QString CONVERT_STRING  = QStringLiteral ( "vje-convert-string" );
		inline const QString CONVERT_NUMBER  = QStringLiteral ( "vje-convert-number" );
		inline const QString CONVERT_BOOLEAN = QStringLiteral ( "vje-convert-boolean" );
		inline const QString CONVERT_NULL    = QStringLiteral ( "vje-convert-null" );

		// Edit > Copy JSONPath Result (QUERY-08). Two sheets with a dollar sign on the front one -- edit-copy's two
		// sheets carrying JSONPath's root operator, which is vje-copy-pointer's family rule: two sheets plus a mark
		// saying WHICH copy this is.

		inline const QString COPY_QUERY_RESULT = QStringLiteral ( "jsonpath-copy-result" );

		// The editor views' tab glyphs (STYLE-06). Told apart by SILHOUETTE, since four tabs side by side are
		// distinguished at a glance or not at all.
		//
		// THEY ARE CONSTANTS HERE RATHER THAN LITERALS AT THEIR VIEWS, which they were until 2026-08-16. Each name was
		// spelled once in its provider's icon_name() and again in tst_icon_library's list, and that duplication is
		// exactly how the JSONPath glyph went missing: the provider said "vje-view-query", the artwork said
		// "vje-view-jsonpath", and nothing compared the two because a name the set does not hold is a legitimate state
		// (the tab shows its label alone). One spelling closes that.

		inline const QString VIEW_FORM     = QStringLiteral ( "vje-view-form" );
		inline const QString VIEW_TEXT     = QStringLiteral ( "vje-view-text" );
		inline const QString VIEW_CODE     = QStringLiteral ( "vje-view-code" );
		inline const QString VIEW_JSONPATH = QStringLiteral ( "vje-view-jsonpath" );

		// The editor tabs' close button (VIEW-03). CHROME RATHER THAN A COMMAND -- it is the only name here that no
		// QAction carries and that the SET-04 toolbar catalogue therefore never offers, since the catalogue's entry
		// ticket is "a command with a glyph" and this is a glyph with no command. (The four view glyphs above DO
		// carry actions and became toolbar-eligible on 2026-08-16; this one still does not.) It is a plain x, deliberately not
		// DOCUMENT_CLOSE (File > Close's document-with-an-x), because what a tab close dismisses is a view and not a
		// document.

		inline const QString TAB_CLOSE = QStringLiteral ( "tab-close" );
	}

	//*****************************************************************************************************************
	// Class: IconLibrary
	//*****************************************************************************************************************

	class IconLibrary : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Binds to the theme so the cache is dropped and icons re-tint whenever a palette is applied. Passing nullptr
		// is legal and yields a library that tints from the current QApplication palette but never invalidates -- the
		// form the headless tests use.

		explicit IconLibrary ( ThemeService* theme, QObject* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The tinted icon for a resource base name (see icon_names). Returns a null QIcon for an unknown name; callers
		// setting an icon on a QAction need no guard, as a null icon simply shows nothing.

		QIcon icon ( const QString& name ) const;

		bool has_icon ( const QString& name ) const;

		// Every name compiled into the resource for one family, sorted. Static because it asks the resource system
		// rather than this object; the family is a parameter for the same reason, defaulted so a caller that does not
		// care is unaffected.
		//
		// Under Fluent this is the UNION of the vendored Microsoft tree and the custom one, de-duplicated. Skipping the
		// de-duplication would report a name twice for every glyph both trees hold during the vendoring, which reads
		// as a missing-file bug rather than as a union bug.

		static QStringList available_icons
		(
			config::appearance::InterfaceStyle style = config::appearance::DEFAULT_INTERFACE_STYLE
		);

		config::appearance::InterfaceStyle interface_style () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Change which FAMILY is read (SET-13), dropping the cache and announcing it exactly as a theme application
		// does -- so every consumer already listening for icons_changed re-fetches with no wiring of its own. A no-op
		// when the style has not actually changed, so an unrelated Settings OK does not rebuild 47 glyphs.
		//
		// PUSHED IN rather than read from a settings store here, which is the rule ThemeService and views/Card already
		// follow for the other halves of this same setting: services/settings_profiles reaches into vje_core, and
		// dragging that into a class eleven test targets compile is a dependency bought for one enum. The composition
		// root reads it once and hands it to all three.

		void set_interface_style ( config::appearance::InterfaceStyle style );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The tint or the family changed; every icon previously handed out is stale. Consumers re-fetch what they hold.

		void icons_changed ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		QIcon build_icon ( const QString& name ) const;

		// The two families' per-size work. Each adds one glyph at one DEVICE size in BOTH tints, or nothing at all
		// where the family cannot supply it -- a missing asset is skipped, never fatal, which is what lets Fluent run
		// on a partially-vendored Microsoft tree and Classic on a ladder rung nobody has drawn yet.

		static void add_vector_size
		(
			QIcon&            icon,
			const QByteArray& source,
			int               deviceSize,
			const QColor&     normalColour,
			const QColor&     disabledColour
		);

		static void add_raster_size ( QIcon& icon, const QString& name, int deviceSize );

		// Classic's disabled form: the same drawing at config::icons::DISABLED_ARTWORK_OPACITY. Ours rather than the
		// toolkit's, because QStyle::generatedIconPixmap drains the colour a Classic glyph exists to carry.

		static QPixmap faded_artwork ( const QImage& master );

		// The Fluent path: one source file per DESIGN size, rasterized per rung with the tint substituted into it.

		static QByteArray load_vector_source ( const QString& name, int designSize );
		static QPixmap    render_glyph       ( const QByteArray& source, int pixelSize, const QColor& colour );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		ThemeService*                      theme;            // Non-owning; may be null.
		config::appearance::InterfaceStyle interfaceStyle;   // Which family is read (SET-13). Pushed in, never read.
		mutable QHash<QString, QIcon>      cache;            // Built on demand, dropped on every theme application.
	};
}

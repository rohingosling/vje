//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   IconLibrary -- the bundled, theme-aware icon set shared by the menu, toolbar, and tree (TREE-03, STYLE-06).
//
//   The assets are monochrome glyphs compiled into the binary as Qt resources. They carry no colour of their own, and
//   this service applies the active palette's text colour at load time -- which is what satisfies "menu, toolbar, and
//   tree icons are theme-aware and recolour with the theme" (spec section 2.9), a requirement QIcon::fromTheme cannot
//   meet since it has no way to tint.
//
//   TWO SOURCES, one behaviour. config::icons::SOURCE_FORMAT selects which is read; both are committed and both are
//   compiled in, so switching is one constant and a rebuild:
//
//     Svg   assets/images/icons/svg/<grid>/<name>.svg -- each draws with fill="currentColor", and the palette colour
//           is substituted into the source before QSvgRenderer rasterizes it.
//     Png   assets/images/icons/png/<grid>/<size>/<name>.png -- already rasterized. Only the alpha channel is read;
//           the palette colour is composited over it (tint_mask).
//
//   The two carry the SAME PIXELS, exactly, at every size either master is drawn for -- config::icons::ARTWORK_SOURCE
//   names which one is the artwork and the other is generated from it. So the choice changes when rasterization
//   happened, not what appears on screen; see AppConfig.hpp for what does turn on it.
//
//   TWO MASTERS, one per logical size the application asks for (config::icons::GRIDS: 16 and 20). They are separate
//   artwork, because a glyph is crisp only when its edges cover whole device pixels -- which holds by construction for
//   artwork drawn on a grid whose unit IS a pixel at that grid's own size. Crispness then survives integer multiples
//   and nothing else, so the 16 master serves 16/32/48/64 and the 20 master 20/40/60/80.
//   Both go into the SAME QIcon and QIcon matches by exact size, so icon() stays a lookup by name: no caller has to
//   know which master answers it.
//
//   Rendering is done at that fixed set of sizes rather than through a custom QIconEngine. QIcon picks the exact-match
//   entry for the device pixels it needs, so a 20 px toolbar button on a 2x display selects the real 40 px render
//   instead of upscaling -- correct HiDPI at integer ratios without the failure modes of a hand-written engine. A
//   FRACTIONAL ratio still falls between renders; closing that is the QIconEngine work parked in Phase 15, and it is
//   reachable only from the SVG source, since a pre-rasterized set has nothing left to re-render.
//
//   Icons are cached per name and the cache is dropped whenever ThemeService reapplies a palette, including the
//   OS-driven repaint under the System theme.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

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

		static QStringList available_icons ();   // Every name compiled into the resource, sorted.

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The tint changed; every icon previously handed out is stale. Consumers re-fetch the icons they hold.

		void icons_changed ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		QIcon build_icon ( const QString& name ) const;

		// The vector path: one source per grid, rasterized with the tint substituted into it.

		static QByteArray load_source  ( const QString& name, int grid );
		static QPixmap    render_glyph ( const QByteArray& source, int pixelSize, const QColor& color );

		// The raster path: one file per size, carrying the glyph in its alpha channel, tinted after loading.

		static QImage  load_mask ( const QString& name, int grid, int pixelSize );
		static QPixmap tint_mask ( const QImage& mask, const QColor& color );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		ThemeService*                 theme;   // Non-owning; may be null.
		mutable QHash<QString, QIcon> cache;   // Built on demand, dropped on every theme application.
	};
}

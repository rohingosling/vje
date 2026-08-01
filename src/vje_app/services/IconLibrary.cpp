//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   IconLibrary implementation -- see IconLibrary.hpp.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/IconLibrary.hpp"
#include "services/ThemeService.hpp"

#include "AppConfig.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRectF>
#include <QStringList>
#include <QSvgRenderer>

namespace vje
{
	namespace
	{
		using config::icons::IconSource;

		// Where the generated set is compiled in (see src/vje_app/CMakeLists.txt). The resource tree mirrors the asset
		// tree exactly, so a resource path states both which SOURCE it came from and which master drew it:
		//
		//   :/vje/icons/svg/<grid>/<name>.svg          one file per grid, rasterized at each size
		//   :/vje/icons/png/<grid>/<size>/<name>.png   one file per size, already rasterized
		//
		// Both are compiled in; which one is read is a run-time choice starting at config::icons::DEFAULT_ICON_SOURCE.

		const QString ICON_RESOURCE_ROOT = QStringLiteral ( ":/vje/icons/" );

		const QString SVG_SUFFIX = QStringLiteral ( ".svg" );
		const QString PNG_SUFFIX = QStringLiteral ( ".png" );

		bool uses_vector_source ( IconSource source )
		{
			return source == IconSource::Svg;
		}

		const QString& icon_suffix ( IconSource source )
		{
			return uses_vector_source ( source ) ? SVG_SUFFIX : PNG_SUFFIX;
		}

		// The directory holding one master's glyphs at one size. The SVG set has no size level -- a single file serves
		// every size -- so pixelSize is simply not part of its path.

		QString icon_resource_directory ( int grid, int pixelSize, IconSource source )
		{
			if ( uses_vector_source ( source ) )
			{
				return ICON_RESOURCE_ROOT + QStringLiteral ( "svg/" ) + QString::number ( grid );
			}

			return ICON_RESOURCE_ROOT + QStringLiteral ( "png/" ) + QString::number ( grid ) + QLatin1Char ( '/' )
			     + QString::number ( pixelSize );
		}

		QString icon_resource_path ( const QString& name, int grid, int pixelSize, IconSource source )
		{
			return icon_resource_directory ( grid, pixelSize, source ) + QLatin1Char ( '/' ) + name
			     + icon_suffix ( source );
		}

		// The size at which a master is asked about when the question is "does this glyph exist" rather than "draw it".
		// Every master is exported at its own grid (SCALE_MULTIPLES starts at 1), so that size always exists.

		int base_size ( int grid )
		{
			return grid;
		}

		// The token every generated glyph paints with, replaced by the palette colour at load time.

		const QByteArray COLOUR_TOKEN = QByteArrayLiteral ( "currentColor" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	IconLibrary::IconLibrary ( ThemeService* theme, QObject* parent )
		: QObject    ( parent )
		, theme      ( theme )
		, iconSource ( config::icons::DEFAULT_ICON_SOURCE )
	{
		// Every palette application invalidates the tint -- including the OS-driven repaint under the System theme,
		// which is why this listens to applied() rather than theme_changed().

		if ( theme != nullptr )
		{
			connect
			(
				theme, &ThemeService::applied,
				this,  [ this ] ()
				{
					cache.clear ();

					emit icons_changed ();
				}
			);
		}
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QIcon IconLibrary::icon ( const QString& name ) const
	{
		const auto cached = cache.constFind ( name );

		if ( cached != cache.constEnd () )
		{
			return cached.value ();
		}

		const QIcon built = build_icon ( name );

		cache.insert ( name, built );

		return built;
	}

	bool IconLibrary::has_icon ( const QString& name ) const
	{
		// Every glyph exists in every master -- the generator's name check refuses to emit a set where it does not --
		// so one grid answers for all of them.

		const int grid = config::icons::GRIDS [ 0 ];

		return QFile::exists ( icon_resource_path ( name, grid, base_size ( grid ), iconSource ) );
	}

	QStringList IconLibrary::available_icons ( config::icons::IconSource source )
	{
		QStringList names;

		const int grid = config::icons::GRIDS [ 0 ];

		const QDir        directory ( icon_resource_directory ( grid, base_size ( grid ), source ) );
		const QStringList entries   =
			directory.entryList ( { QStringLiteral ( "*%1" ).arg ( icon_suffix ( source ) ) }, QDir::Files, QDir::Name );

		names.reserve ( entries.size () );

		for ( const QString& entry : entries )
		{
			names.append ( entry.left ( entry.size () - icon_suffix ( source ).size () ) );
		}

		return names;
	}

	config::icons::IconSource IconLibrary::source () const
	{
		return iconSource;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void IconLibrary::set_source ( config::icons::IconSource source )
	{
		if ( source == iconSource )
		{
			return;
		}

		iconSource = source;

		cache.clear ();

		// The same announcement a theme application makes, and deliberately so: every icon consumer already listens for
		// it, so switching source needs no wiring of its own.

		emit icons_changed ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	QIcon IconLibrary::build_icon ( const QString& name ) const
	{
		// One glyph, two tints. WindowText is the single foreground role the whole set renders against -- menus and
		// toolbars share it under Fusion, and one tint per mode is what lets a single asset serve every surface.

		const QPalette palette = QGuiApplication::palette ();

		const QColor normalColour   = palette.color ( QPalette::Active,   QPalette::WindowText );
		const QColor disabledColour = palette.color ( QPalette::Disabled, QPalette::WindowText );

		QIcon icon;

		// Both masters go into the SAME QIcon, each rendered only at multiples of its own grid. QIcon then matches by
		// exact size, so a 16 px consumer is served by the 16 master and a 20 px consumer by the 20 one without either
		// having to know the other exists -- which is what keeps icon() a lookup by name alone.
		//
		// WHAT IS MISSING IS SKIPPED, NOT FATAL (2026-07-31). Every absent source used to abandon the whole glyph, and
		// under the raster source that made a PARTIAL LADDER indistinguishable from a broken set: a png tree carrying
		// only the x1 sizes built its 16 px pixmap, hit the absent x2, and returned a null QIcon -- discarding the good
		// pixmap it already had. Every button in the application went blank while has_icon() still answered yes, since
		// that only ever asks about x1.
		//
		// The ladder is an OPTIMIZATION, not a requirement. Its whole purpose is to hand QIcon an exact-size render at
		// integer device pixel ratios; where a rung is absent QIcon scales the nearest one, which is worse-looking and
		// entirely functional. Trading every icon in the application for that is not a trade worth making, so a missing
		// size costs that size alone. An icon with no size at all is still null, which is what keeps an unknown name
		// answering with a blank rather than with someone else's glyph (unknown_name_returns_a_null_icon).

		for ( const int grid : config::icons::GRIDS )
		{
			// The vector source is read ONCE PER GRID and rasterized at each size; the raster source is one file per
			// size and is read inside add_size. Reading it here is what keeps the SVG path at two file reads per icon
			// rather than eight.

			const QByteArray vectorSource =
				uses_vector_source ( iconSource ) ? load_source ( name, grid ) : QByteArray ();

			if ( uses_vector_source ( iconSource ) && vectorSource.isEmpty () )
			{
				continue;
			}

			for ( const int multiple : config::icons::SCALE_MULTIPLES )
			{
				add_size ( icon, name, grid, grid * multiple, vectorSource, normalColour, disabledColour );
			}
		}

		return icon;
	}

	void IconLibrary::add_size
	(
		QIcon&            icon,
		const QString&    name,
		int               grid,
		int               pixelSize,
		const QByteArray& vectorSource,
		const QColor&     normalColour,
		const QColor&     disabledColour
	) const
	{
		QPixmap normalPixmap;
		QPixmap disabledPixmap;

		if ( uses_vector_source ( iconSource ) )
		{
			if ( vectorSource.isEmpty () )
			{
				return;
			}

			// Two rasterizations rather than one, and unavoidably so: the tint is substituted INTO the SVG text before
			// it is parsed, so the two tints are two different documents. The file read they share is the saved cost.

			normalPixmap   = render_glyph ( vectorSource, pixelSize, normalColour );
			disabledPixmap = render_glyph ( vectorSource, pixelSize, disabledColour );
		}
		else
		{
			// One decode, two tints -- the mask is the shape and the colour is applied over it, so asking for the file
			// twice would decode the same pixels twice for no gain.

			const QImage mask = load_mask ( name, grid, pixelSize, iconSource );

			normalPixmap   = tint_mask ( mask, normalColour );
			disabledPixmap = tint_mask ( mask, disabledColour );
		}

		if ( normalPixmap.isNull () )
		{
			return;
		}

		// Supplying the Disabled pixmaps explicitly keeps greyed commands tinted from the palette rather than from Qt's
		// washed-out fallback -- which matters here, where most of the shell's actions start disabled.

		icon.addPixmap ( normalPixmap,   QIcon::Normal,   QIcon::Off );
		icon.addPixmap ( disabledPixmap, QIcon::Disabled, QIcon::Off );
	}

	QByteArray IconLibrary::load_source ( const QString& name, int grid )
	{
		QFile file ( icon_resource_path ( name, grid, base_size ( grid ), IconSource::Svg ) );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			return QByteArray ();
		}

		return file.readAll ();
	}

	QImage IconLibrary::load_mask ( const QString& name, int grid, int pixelSize, IconSource source )
	{
		// Only the ALPHA channel is used -- see tint_mask -- so the colour the exporter baked in is irrelevant here and
		// a differently-tinted export would load identically.

		return QImage ( icon_resource_path ( name, grid, pixelSize, source ) );
	}

	QPixmap IconLibrary::tint_mask ( const QImage& mask, const QColor& colour )
	{
		// The raster counterpart of render_glyph's currentColor substitution, and it has to reach the same place: the
		// PNG carries a shape in its alpha channel, and the palette decides the colour at load time (spec section 2.9).
		//
		// CompositionMode_SourceIn keeps the DESTINATION's alpha and takes the SOURCE's colour, so filling the whole
		// rect after drawing the mask replaces every inked pixel's colour while leaving its coverage exactly as
		// rasterized. The antialiased edges therefore survive as edges of the NEW colour -- which is why the baked
		// colour cannot bleed through and leave a dark fringe on a light glyph.

		if ( mask.isNull () )
		{
			return QPixmap ();
		}

		QImage tinted ( mask.size (), QImage::Format_ARGB32_Premultiplied );

		tinted.fill ( Qt::transparent );

		QPainter painter ( &tinted );

		painter.drawImage ( 0, 0, mask );
		painter.setCompositionMode ( QPainter::CompositionMode_SourceIn );
		painter.fillRect ( tinted.rect (), colour );
		painter.end ();

		return QPixmap::fromImage ( tinted );
	}

	QPixmap IconLibrary::render_glyph ( const QByteArray& source, int pixelSize, const QColor& colour )
	{
		// The glyphs carry no colour of their own -- substituting the token is the whole tinting mechanism.

		QByteArray tinted = source;

		tinted.replace ( COLOUR_TOKEN, colour.name ( QColor::HexRgb ).toLatin1 () );

		QSvgRenderer renderer ( tinted );

		if ( !renderer.isValid () )
		{
			return QPixmap ();
		}

		QPixmap pixmap ( pixelSize, pixelSize );

		pixmap.fill ( Qt::transparent );

		QPainter painter ( &pixmap );

		// Set for completeness rather than for effect. QPainter::Antialiasing has no influence on QSvgRenderer, which
		// antialiases regardless of the painter's hint (measured) -- and it would have nothing to do here in any case:
		// the masters are drawn on a grid whose unit IS a pixel at that grid's own size, so at every size this renders
		// every edge falls on a pixel boundary and coverage is exactly 0 or 1
		// (tst_icon_library::glyphs_are_pixel_crisp_at_every_authored_size).

		painter.setRenderHint ( QPainter::Antialiasing, true );

		renderer.render ( &painter, QRectF ( 0, 0, pixelSize, pixelSize ) );

		return pixmap;
	}
}

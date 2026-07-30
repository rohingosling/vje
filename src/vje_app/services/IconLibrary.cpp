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
		// Where the generated set is compiled in (see src/vje_app/CMakeLists.txt). One subdirectory per authored grid,
		// named for the grid, so a resource path states which master it came from.

		const QString ICON_RESOURCE_ROOT = QStringLiteral ( ":/vje/icons/" );
		const QString ICON_SUFFIX        = QStringLiteral ( ".svg" );

		QString icon_resource_path ( const QString& name, int grid )
		{
			return ICON_RESOURCE_ROOT + QString::number ( grid ) + QLatin1Char ( '/' ) + name + ICON_SUFFIX;
		}

		// The token every generated glyph paints with, replaced by the palette colour at load time.

		const QByteArray COLOUR_TOKEN = QByteArrayLiteral ( "currentColor" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	IconLibrary::IconLibrary ( ThemeService* theme, QObject* parent )
		: QObject ( parent )
		, theme   ( theme )
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

		return QFile::exists ( icon_resource_path ( name, config::icons::GRIDS [ 0 ] ) );
	}

	QStringList IconLibrary::available_icons ()
	{
		QStringList names;

		const QDir        directory ( ICON_RESOURCE_ROOT + QString::number ( config::icons::GRIDS [ 0 ] ) );
		const QStringList entries = directory.entryList ( { QStringLiteral ( "*%1" ).arg ( ICON_SUFFIX ) }, QDir::Files, QDir::Name );

		names.reserve ( entries.size () );

		for ( const QString& entry : entries )
		{
			names.append ( entry.left ( entry.size () - ICON_SUFFIX.size () ) );
		}

		return names;
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

		for ( const int grid : config::icons::GRIDS )
		{
			const QByteArray source = load_source ( name, grid );

			if ( source.isEmpty () )
			{
				return QIcon ();
			}

			for ( const int multiple : config::icons::SCALE_MULTIPLES )
			{
				const int pixelSize = grid * multiple;

				const QPixmap normalPixmap   = render_glyph ( source, pixelSize, normalColour );
				const QPixmap disabledPixmap = render_glyph ( source, pixelSize, disabledColour );

				if ( normalPixmap.isNull () )
				{
					return QIcon ();
				}

				// Supplying the Disabled pixmaps explicitly keeps greyed commands tinted from the palette rather than
				// from Qt's washed-out fallback -- which matters here, where most of the shell's actions start disabled.

				icon.addPixmap ( normalPixmap,   QIcon::Normal,   QIcon::Off );
				icon.addPixmap ( disabledPixmap, QIcon::Disabled, QIcon::Off );
			}
		}

		return icon;
	}

	QByteArray IconLibrary::load_source ( const QString& name, int grid )
	{
		QFile file ( icon_resource_path ( name, grid ) );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			return QByteArray ();
		}

		return file.readAll ();
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

		painter.setRenderHint ( QPainter::Antialiasing, true );

		renderer.render ( &painter, QRectF ( 0, 0, pixelSize, pixelSize ) );

		return pixmap;
	}
}

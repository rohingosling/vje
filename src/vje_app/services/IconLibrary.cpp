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
		using config::appearance::InterfaceStyle;

		// Where each family is compiled in (see src/vje_app/CMakeLists.txt). The resource tree mirrors the asset tree,
		// so a resource path states which family it came from and which drawing answered:
		//
		//   :/vje/icons/classic/<deviceSize>/<name>.png            one drawing per display scaling
		//   :/vje/icons/fluent/microsoft/<designSize>/<name>.svg   vendored, normalised to currentColor
		//   :/vje/icons/fluent/custom/<designSize>/<name>.svg      traced from the reference PNGs

		const QString CLASSIC_ROOT   = QStringLiteral ( ":/vje/icons/classic/" );
		const QString MICROSOFT_ROOT = QStringLiteral ( ":/vje/icons/fluent/microsoft/" );
		const QString CUSTOM_ROOT    = QStringLiteral ( ":/vje/icons/fluent/custom/" );

		const QString SVG_SUFFIX = QStringLiteral ( ".svg" );
		const QString PNG_SUFFIX = QStringLiteral ( ".png" );

		QString classic_resource_directory ( int deviceSize )
		{
			return CLASSIC_ROOT + QString::number ( deviceSize );
		}

		QString classic_resource_path ( const QString& name, int deviceSize )
		{
			return classic_resource_directory ( deviceSize ) + QLatin1Char ( '/' ) + name + PNG_SUFFIX;
		}

		QString microsoft_resource_directory ( int designSize )
		{
			return MICROSOFT_ROOT + QString::number ( designSize );
		}

		QString custom_resource_directory ( int designSize )
		{
			return CUSTOM_ROOT + QString::number ( designSize );
		}

		// THE FLUENT PRECEDENCE, stated once. Microsoft's vendored glyph wins where one exists; the custom tree is the
		// fallback bed that keeps the family complete while the vendoring is only partly done. Returns an empty string
		// when neither holds the name, which the callers treat as "skip this size" rather than as an error.

		QString fluent_resource_path ( const QString& name, int designSize )
		{
			const QString vendored = microsoft_resource_directory ( designSize ) + QLatin1Char ( '/' ) + name + SVG_SUFFIX;

			if ( QFile::exists ( vendored ) )
			{
				return vendored;
			}

			const QString custom = custom_resource_directory ( designSize ) + QLatin1Char ( '/' ) + name + SVG_SUFFIX;

			return QFile::exists ( custom ) ? custom : QString ();
		}

		// The token every Fluent glyph paints with, replaced by the palette colour at load time. The custom set is
		// traced with it; the vendored set is rewritten to it by tools/pull_microsoft_icons.py, which is why one token
		// answers for both sub-sources.

		const QByteArray COLOUR_TOKEN = QByteArrayLiteral ( "currentColor" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	IconLibrary::IconLibrary ( ThemeService* theme, QObject* parent )
		: QObject        ( parent )
		, theme          ( theme )
		, interfaceStyle ( config::appearance::DEFAULT_INTERFACE_STYLE )
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
		// One rung answers for all of them: every glyph exists at every size of whichever family is current, which is
		// what every_family_carries_every_name asserts rather than what a generator promises.

		return ( interfaceStyle == InterfaceStyle::Classic )
		     ? QFile::exists ( classic_resource_path ( name, config::icons::LADDER [ 0 ].deviceSize ) )
		     : !fluent_resource_path ( name, config::icons::LADDER [ 0 ].designSize ).isEmpty ();
	}

	QStringList IconLibrary::available_icons ( config::appearance::InterfaceStyle style )
	{
		const auto names_in = [] ( const QString& directoryPath, const QString& suffix )
		{
			QStringList found;

			const QDir        directory ( directoryPath );
			const QStringList entries   =
				directory.entryList ( { QStringLiteral ( "*%1" ).arg ( suffix ) }, QDir::Files, QDir::Name );

			found.reserve ( entries.size () );

			for ( const QString& entry : entries )
			{
				found.append ( entry.left ( entry.size () - suffix.size () ) );
			}

			return found;
		};

		if ( style == InterfaceStyle::Classic )
		{
			return names_in ( classic_resource_directory ( config::icons::LADDER [ 0 ].deviceSize ), PNG_SUFFIX );
		}

		// The UNION of the two Fluent sub-sources, de-duplicated -- a name held by both is one glyph, not two, and
		// reporting it twice would read as a missing file rather than as a double count.

		const int designSize = config::icons::LADDER [ 0 ].designSize;

		QStringList names = names_in ( microsoft_resource_directory ( designSize ), SVG_SUFFIX );

		for ( const QString& name : names_in ( custom_resource_directory ( designSize ), SVG_SUFFIX ) )
		{
			if ( !names.contains ( name ) )
			{
				names.append ( name );
			}
		}

		names.sort ();

		return names;
	}

	config::appearance::InterfaceStyle IconLibrary::interface_style () const
	{
		return interfaceStyle;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void IconLibrary::set_interface_style ( config::appearance::InterfaceStyle style )
	{
		if ( style == interfaceStyle )
		{
			return;
		}

		interfaceStyle = style;

		cache.clear ();

		// The same announcement a theme application makes, and deliberately so: every icon consumer already listens
		// for it, so switching family needs no wiring of its own.

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

		if ( interfaceStyle == InterfaceStyle::Classic )
		{
			// ONE DRAWN FILE PER RUNG. Each surface gets the master authored for the size it asked for -- the
			// toolbar 20, the tree and the menus 16 -- so nothing is resampled from a neighbouring size. The palette
			// is not consulted at all: a Classic master is used exactly as authored (SET-13a), which is why the two
			// tints are unused here.

			for ( const config::icons::Rung& rung : config::icons::LADDER )
			{
				add_raster_size ( icon, name, rung.deviceSize );
			}
		}
		else
		{
			// One file read per DESIGN, rasterized at each rung that design serves. Reading per design rather than
			// per rung is what keeps this two file reads instead of seven.

			for ( const int designSize : config::icons::DESIGN_SIZES )
			{
				const QByteArray source = load_vector_source ( name, designSize );

				if ( source.isEmpty () )
				{
					continue;
				}

				for ( const config::icons::Rung& rung : config::icons::LADDER )
				{
					if ( rung.designSize == designSize )
					{
						add_vector_size ( icon, source, rung.deviceSize, normalColour, disabledColour );
					}
				}
			}
		}

		return icon;
	}

	void IconLibrary::add_vector_size
	(
		QIcon&            icon,
		const QByteArray& source,
		int               deviceSize,
		const QColor&     normalColour,
		const QColor&     disabledColour
	)
	{
		// Two rasterizations are unavoidable: the tint is substituted into the SVG TEXT before it is parsed, so the
		// two tints are two different documents.

		const QPixmap normalPixmap = render_glyph ( source, deviceSize, normalColour );

		if ( normalPixmap.isNull () )
		{
			return;
		}

		icon.addPixmap ( normalPixmap,                                        QIcon::Normal,   QIcon::Off );
		icon.addPixmap ( render_glyph ( source, deviceSize, disabledColour ), QIcon::Disabled, QIcon::Off );
	}

	void IconLibrary::add_raster_size ( QIcon& icon, const QString& name, int deviceSize )
	{
		const QImage master ( classic_resource_path ( name, deviceSize ) );

		if ( master.isNull () )
		{
			return;   // An undrawn rung costs that rung, not the glyph.
		}

		// Used exactly as authored (SET-13a). The disabled form fades the same drawing rather than draining it --
		// see config::icons::DISABLED_ARTWORK_OPACITY.
		//
		// BOTH MODES ARE ADDED AT EVERY RUNG, and that is load-bearing rather than tidy: QIcon matches MODE FIRST and
		// SIZE SECOND, so an absent Disabled entry at one size does not fall back to that size's Normal pixmap -- it
		// reaches for a Disabled entry at ANOTHER size and scales it. Measured with a probe, and it bit for real.

		icon.addPixmap ( QPixmap::fromImage ( master ), QIcon::Normal,   QIcon::Off );
		icon.addPixmap ( faded_artwork      ( master ), QIcon::Disabled, QIcon::Off );
	}

	QPixmap IconLibrary::faded_artwork ( const QImage& master )
	{
		QImage faded ( master.size (), QImage::Format_ARGB32_Premultiplied );

		faded.fill ( Qt::transparent );

		QPainter painter ( &faded );

		painter.setOpacity ( config::icons::DISABLED_ARTWORK_OPACITY );
		painter.drawImage ( 0, 0, master );
		painter.end ();

		return QPixmap::fromImage ( faded );
	}

	QByteArray IconLibrary::load_vector_source ( const QString& name, int designSize )
	{
		const QString path = fluent_resource_path ( name, designSize );

		if ( path.isEmpty () )
		{
			return QByteArray ();
		}

		QFile file ( path );

		return file.open ( QIODevice::ReadOnly ) ? file.readAll () : QByteArray ();
	}

	QPixmap IconLibrary::render_glyph ( const QByteArray& source, int pixelSize, const QColor& colour )
	{
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

		// Measured to have no effect on QSvgRenderer, which antialiases regardless; set for completeness so the
		// intent is not mistaken for an omission.

		painter.setRenderHint ( QPainter::Antialiasing, true );

		renderer.render ( &painter, QRectF ( 0, 0, pixelSize, pixelSize ) );

		painter.end ();

		return pixmap;
	}
}

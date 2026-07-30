//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   IconLibrary unit tests, run against the REAL shipped assets (the same Qt resource the application compiles).
//
//   Three failure modes drive the choices here, because all three are silent at run time and invisible to a build:
//
//     1. A missing Qt SVG module -- QSvgRenderer refuses the source and every icon comes back null. Asserting that
//        icons are non-null AND actually paint pixels catches it; a build alone would not.
//     2. A drifted name constant -- a typo in icon_names or a renamed asset yields a null icon and a blank button.
//        Every constant is therefore resolved explicitly.
//     3. A broken tint -- the icons keep rendering but stop following the palette, quietly violating "icons recolour
//        with the theme". Compared by pixel between a light and a dark palette.
//
//   Runs under the offscreen QPA platform (set by CTest), so it needs no display.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/IconLibrary.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"

#include "AppConfig.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace vje;

namespace
{
	// The size the assertions rasterize at. It must be one of config::icons::GRIDS -- a size with a master authored
	// for it -- or QIcon scales and the pixels under test are not the pixels that ship.

	constexpr int SAMPLE_SIZE = 16;

	static_assert ( config::icons::is_authored_grid ( SAMPLE_SIZE ) );

	// Every name the application addresses. Resolving all of them is what turns a renamed or missing asset into a
	// test failure instead of a blank button noticed weeks later.

	QStringList all_icon_name_constants ()
	{
		return
		{
			icon_names::DOCUMENT_NEW,   icon_names::DOCUMENT_OPEN,       icon_names::DOCUMENT_SAVE,
			icon_names::DOCUMENT_SAVE_AS, icon_names::DOCUMENT_CLOSE,    icon_names::DOCUMENT_PRINT,
			icon_names::DOCUMENT_PAGE_SETUP,

			icon_names::EDIT_UNDO,      icon_names::EDIT_REDO,           icon_names::EDIT_CUT,
			icon_names::EDIT_COPY,      icon_names::EDIT_PASTE,          icon_names::EDIT_FIND,
			icon_names::GO_TO,          icon_names::COPY_POINTER,

			icon_names::NODE_DELETE,    icon_names::NODE_DUPLICATE,      icon_names::NODE_RENAME,
			icon_names::NODE_MOVE_UP,   icon_names::NODE_MOVE_DOWN,

			icon_names::NORMALIZE_ARRAY, icon_names::ARRAY_TO_OBJECTS,   icon_names::OBJECTS_TO_ARRAY,

			icon_names::EXPAND_ALL,     icon_names::COLLAPSE_ALL,
			icon_names::SETTINGS,       icon_names::ABOUT,

			// The three view tabs are addressed by literal at their view (FormView / TextView / CodeView's
			// view_icon_name()) rather than through icon_names, so they are spelled the same way here.

			QStringLiteral ( "vje-view-form" ),
			QStringLiteral ( "vje-view-text" ),
			QStringLiteral ( "vje-view-code" ),

			icon_names::TYPE_DOCUMENT,
			icon_names::TYPE_OBJECT,    icon_names::TYPE_ARRAY,          icon_names::TYPE_STRING,
			icon_names::TYPE_NUMBER,    icon_names::TYPE_BOOLEAN,        icon_names::TYPE_NULL,

			icon_names::ADD_OBJECT,     icon_names::ADD_ARRAY,           icon_names::ADD_STRING,
			icon_names::ADD_NUMBER,     icon_names::ADD_BOOLEAN,         icon_names::ADD_NULL
		};
	}

	// How many pixels the glyph actually painted. Zero means "rendered, but blank" -- the failure a null check misses.

	int opaque_pixel_count ( const QImage& image )
	{
		int count = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				if ( qAlpha ( image.pixel ( x, y ) ) > 0 )
				{
					++count;
				}
			}
		}

		return count;
	}

	// Of the pixels carrying any ink, how many are FULLY opaque? This is the crispness measure: a glyph drawn on its
	// own pixel grid has a solid core with antialiasing only at its curves and diagonals, while one rendered at a size
	// it was not authored for is all edge and no core.

	double solid_core_fraction ( const QImage& image )
	{
		long long inked = 0;
		long long solid = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				const int alpha = qAlpha ( image.pixel ( x, y ) );

				if ( alpha == 0 ) continue;

				++inked;

				if ( alpha >= 250 ) ++solid;
			}
		}

		return inked > 0 ? double ( solid ) / double ( inked ) : 0.0;
	}
}

//*********************************************************************************************************************
// Class: TestIconLibrary
//*********************************************************************************************************************

class TestIconLibrary : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;
	std::unique_ptr<IconLibrary>   icons;

private slots:

	void init ()
	{
		temporaryDirectory = std::make_unique<QTemporaryDir> ();

		QVERIFY ( temporaryDirectory->isValid () );

		settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
		theme    = std::make_unique<ThemeService>  ( settings.get () );
		icons    = std::make_unique<IconLibrary>   ( theme.get () );

		// Start from a known palette so the tint comparisons below are deterministic.

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	void cleanup ()
	{
		icons.reset ();
		theme.reset ();
		settings.reset ();
		temporaryDirectory.reset ();
	}

	//=================================================================================================================
	// Resource presence
	//=================================================================================================================

	void resource_exposes_the_generated_set ()
	{
		const QStringList available = IconLibrary::available_icons ();

		QVERIFY2 ( !available.isEmpty (), "the icon resource is empty -- were the icons compiled into the resource?" );

		// Every addressed name must exist as an asset.

		for ( const QString& name : all_icon_name_constants () )
		{
			QVERIFY2
			(
				available.contains ( name ),
				qPrintable ( QStringLiteral ( "missing icon asset: %1" ).arg ( name ) )
			);
		}
	}

	void has_icon_reports_presence ()
	{
		QVERIFY ( icons->has_icon ( icon_names::DOCUMENT_OPEN ) );
		QVERIFY ( !icons->has_icon ( QStringLiteral ( "no-such-icon" ) ) );
	}

	// has_icon() and available_icons() both answer from ONE master, which is only sound while every master carries
	// every name. The generator refuses to emit a set where it does not; this is the same claim checked against what
	// actually shipped, so a hand-added or hand-deleted asset cannot slip past it.

	void every_master_carries_every_name ()
	{
		QStringList reference;

		for ( const int grid : config::icons::GRIDS )
		{
			const QDir directory ( QStringLiteral ( ":/vje/icons/%1" ).arg ( grid ) );

			QStringList names = directory.entryList ( { QStringLiteral ( "*.svg" ) }, QDir::Files, QDir::Name );

			QVERIFY2
			(
				!names.isEmpty (),
				qPrintable ( QStringLiteral ( "no assets compiled in for the %1 master" ).arg ( grid ) )
			);

			if ( reference.isEmpty () )
			{
				reference = names;
			}
			else
			{
				QCOMPARE ( names, reference );
			}
		}
	}

	//=================================================================================================================
	// Crispness -- the property the two-master split exists to deliver.
	//=================================================================================================================

	// SVG has no hinting, so a glyph is only pixel-crisp at the grid it was drawn on and at integer multiples of it.
	// Asking each master for its OWN size must therefore yield a solid core rather than a field of antialiasing.
	//
	// The floor discriminates against the set this replaced rather than being a round number: the single 24-unit
	// master with stroke-width 1.6 measured 5.4% solid at 16 px and 20.9% at 20 px, and the two masters measure 51%
	// and 53%. Anything between those two bands would catch a regression to the old scheme; 35% sits in the middle
	// with room for the geometry to be retuned without the test becoming a tripwire.

	void glyphs_are_pixel_crisp_at_every_authored_size ()
	{
		constexpr double SOLID_CORE_FLOOR = 0.35;

		for ( const int grid : config::icons::GRIDS )
		{
			long long inked = 0;
			long long solid = 0;

			for ( const QString& name : all_icon_name_constants () )
			{
				const QPixmap pixmap = icons->icon ( name ).pixmap ( QSize ( grid, grid ), 1.0 );

				QCOMPARE ( pixmap.size (), QSize ( grid, grid ) );

				const QImage image = pixmap.toImage ();

				for ( int y = 0; y < image.height (); ++y )
				{
					for ( int x = 0; x < image.width (); ++x )
					{
						const int alpha = qAlpha ( image.pixel ( x, y ) );

						if ( alpha == 0 ) continue;

						++inked;

						if ( alpha >= 250 ) ++solid;
					}
				}
			}

			QVERIFY ( inked > 0 );

			const double fraction = double ( solid ) / double ( inked );

			QVERIFY2
			(
				fraction >= SOLID_CORE_FLOOR,
				qPrintable ( QStringLiteral ( "the %1 master is only %2% solid at %1 px -- it is being rendered at a "
				                              "size it was not authored for, or its geometry has left the half-integer grid" )
				             .arg ( grid ).arg ( fraction * 100.0, 0, 'f', 1 ) )
			);
		}
	}

	// The same glyph asked for at the OTHER master's size still has to render -- QIcon scales rather than failing --
	// but this pins the reason both masters exist: each is crisp at its own size, and neither covers the other's.

	void a_single_glyph_is_solid_at_both_authored_sizes ()
	{
		for ( const int grid : config::icons::GRIDS )
		{
			const QImage image = icons->icon ( icon_names::NODE_MOVE_UP )
				.pixmap ( QSize ( grid, grid ), 1.0 ).toImage ();

			QVERIFY2
			(
				solid_core_fraction ( image ) > 0.35,
				qPrintable ( QStringLiteral ( "go-up is soft at %1 px" ).arg ( grid ) )
			);
		}
	}

	//=================================================================================================================
	// Rendering -- catches a missing Qt SVG module, which fails silently rather than at link time.
	//=================================================================================================================

	void every_named_icon_renders_visible_pixels ()
	{
		for ( const QString& name : all_icon_name_constants () )
		{
			const QIcon icon = icons->icon ( name );

			QVERIFY2 ( !icon.isNull (), qPrintable ( QStringLiteral ( "null icon: %1" ).arg ( name ) ) );

			const QPixmap pixmap = icon.pixmap ( QSize ( SAMPLE_SIZE, SAMPLE_SIZE ) );

			QVERIFY2 ( !pixmap.isNull (), qPrintable ( QStringLiteral ( "null pixmap: %1" ).arg ( name ) ) );

			QVERIFY2
			(
				opaque_pixel_count ( pixmap.toImage () ) > 0,
				qPrintable ( QStringLiteral ( "icon rendered blank: %1" ).arg ( name ) )
			);
		}
	}

	void unknown_name_returns_a_null_icon ()
	{
		QVERIFY ( icons->icon ( QStringLiteral ( "no-such-icon" ) ).isNull () );
	}

	//=================================================================================================================
	// Theme awareness -- the requirement QIcon::fromTheme could not have satisfied.
	//=================================================================================================================

	void icons_are_tinted_from_the_palette ()
	{
		const QImage lightImage = icons->icon ( icon_names::DOCUMENT_OPEN )
			.pixmap ( QSize ( SAMPLE_SIZE, SAMPLE_SIZE ) ).toImage ();

		theme->set_theme ( Theme::Dark );

		const QImage darkImage = icons->icon ( icon_names::DOCUMENT_OPEN )
			.pixmap ( QSize ( SAMPLE_SIZE, SAMPLE_SIZE ) ).toImage ();

		QVERIFY2 ( lightImage != darkImage, "icons must recolour when the theme changes" );

		// Same glyph either way -- only the colour moved, so the painted area should be broadly unchanged.

		QVERIFY ( opaque_pixel_count ( lightImage ) > 0 );
		QVERIFY ( opaque_pixel_count ( darkImage )  > 0 );
	}

	void disabled_mode_differs_from_normal ()
	{
		const QIcon icon = icons->icon ( icon_names::DOCUMENT_SAVE );

		const QImage normalImage   = icon.pixmap ( QSize ( SAMPLE_SIZE, SAMPLE_SIZE ), QIcon::Normal   ).toImage ();
		const QImage disabledImage = icon.pixmap ( QSize ( SAMPLE_SIZE, SAMPLE_SIZE ), QIcon::Disabled ).toImage ();

		QVERIFY2 ( normalImage != disabledImage, "disabled commands must render with the disabled palette colour" );
	}

	void applying_a_theme_emits_icons_changed ()
	{
		QSignalSpy spy ( icons.get (), &IconLibrary::icons_changed );

		QVERIFY ( spy.isValid () );

		theme->set_theme ( Theme::Dark );

		QVERIFY2 ( spy.count () >= 1, "a palette application must invalidate the icon cache" );
	}

	void reapplying_the_same_theme_still_invalidates ()
	{
		// The System theme repaints via apply() without the choice changing; the cache must still drop, or an OS
		// light/dark switch would leave stale icons behind.

		QSignalSpy spy ( icons.get (), &IconLibrary::icons_changed );

		theme->apply ();

		QVERIFY ( spy.count () >= 1 );
	}

	//=================================================================================================================
	// Caching
	//=================================================================================================================

	void repeated_requests_return_the_cached_icon ()
	{
		const QIcon first  = icons->icon ( icon_names::EDIT_UNDO );
		const QIcon second = icons->icon ( icon_names::EDIT_UNDO );

		QCOMPARE ( first.cacheKey (), second.cacheKey () );
	}

	void cache_is_dropped_on_a_theme_change ()
	{
		const qint64 lightCacheKey = icons->icon ( icon_names::EDIT_UNDO ).cacheKey ();

		theme->set_theme ( Theme::Dark );

		QVERIFY2
		(
			icons->icon ( icon_names::EDIT_UNDO ).cacheKey () != lightCacheKey,
			"a stale cache would keep serving the previous theme's tint"
		);
	}
};

QTEST_MAIN ( TestIconLibrary )

#include "tst_icon_library.moc"

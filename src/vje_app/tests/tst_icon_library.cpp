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
//   Four failure modes drive the choices here, because all four are silent at run time and invisible to a build:
//
//     1. A missing Qt SVG module -- QSvgRenderer refuses the source and every icon comes back null. Asserting that
//        icons are non-null AND actually paint pixels catches it; a build alone would not.
//     2. A drifted name constant -- a typo in icon_names or a renamed asset yields a null icon and a blank button.
//        Every constant is therefore resolved explicitly.
//     3. A broken tint -- the icons keep rendering but stop following the palette, quietly violating "icons recolour
//        with the theme". Compared by pixel between a light and a dark palette.
//     4. A STALE DERIVED TREE -- the set ships in two forms (config::icons::SOURCE_FORMAT), one of which is generated
//        from the other (config::icons::ARTWORK_SOURCE) by a separate tool. A glyph corrected without re-running that
//        tool leaves the other form drawing the old artwork, invisibly, in whichever configuration is not live. That
//        is invisible by construction, which is exactly why it is asserted here rather than left to be noticed.
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
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSignalSpy>
#include <QSvgRenderer>
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

	// The base names in one resource directory, sorted -- the shared half of "every master carries every name", asked
	// of an SVG master and of each rendered size of a PNG one.

	QStringList resource_base_names ( const QString& directoryPath, const QString& suffix )
	{
		const QDir directory ( directoryPath );

		QStringList names;

		for ( const QString& entry : directory.entryList ( { QStringLiteral ( "*%1" ).arg ( suffix ) },
		                                                   QDir::Files, QDir::Name ) )
		{
			names.append ( entry.left ( entry.size () - suffix.size () ) );
		}

		return names;
	}

	// The reference render for the agreement check: the same QSvgRenderer pass, ground and size that both
	// IconLibrary::render_glyph and tools/export_icon_pngs use.
	//
	// WHETHER THE RESULT IS CUT AT PNG_ALPHA_THRESHOLD DEPENDS ON WHICH TREE IS THE ARTWORK, and it is the caller's
	// call rather than this function's:
	//
	//   Vector -- the SVGs are strokes and their render is antialiased, while the committed PNGs are two-valued. The
	//             cut is the exporter's own, so applying it here is what makes the two comparable at all; without it
	//             all 43 glyphs read as drifted and the check says nothing about whether any of them is.
	//
	//   Raster -- the SVGs are traced from those same two-valued PNGs, so the render has no partial coverage to cut.
	//             Cutting anyway would be actively harmful: it would round a genuinely soft render up to the answer
	//             expected and report a defect as agreement.
	//
	// The tint is irrelevant -- only alpha is compared -- so it is left at the source's own currentColor rather than
	// substituted.

	QImage render_reference ( const QByteArray& svgText, int pixelSize, bool harden )
	{
		QSvgRenderer renderer ( svgText );

		if ( !renderer.isValid () )
		{
			return QImage ();
		}

		QImage image ( pixelSize, pixelSize, QImage::Format_ARGB32 );

		image.fill ( Qt::transparent );

		QPainter painter ( &image );

		painter.setRenderHint ( QPainter::Antialiasing, true );

		renderer.render ( &painter, QRectF ( 0, 0, pixelSize, pixelSize ) );

		painter.end ();

		if ( !harden )
		{
			return image;
		}

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				const bool inked = qAlpha ( image.pixel ( x, y ) ) >= config::icons::PNG_ALPHA_THRESHOLD;

				image.setPixel ( x, y, inked ? qRgba ( 0, 0, 0, 255 ) : qRgba ( 0, 0, 0, 0 ) );
			}
		}

		return image;
	}

	// How many pixels in a render carry PARTIAL coverage. Under a traced set this must be zero at every authored size:
	// every vertex is an integer, so at an integer multiple of the grid every edge lands between pixels and nothing is
	// half-covered. It is the measure that separates "the shapes agree" from "the shapes are the same drawing".

	int partial_coverage_count ( const QImage& image )
	{
		int count = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				const int alpha = qAlpha ( image.pixel ( x, y ) );

				if ( alpha != 0 && alpha != 255 )
				{
					++count;
				}
			}
		}

		return count;
	}

	// Coverage-only equality. Exact rather than tolerant: both sides come from the same rasterizer at the same size, so
	// any difference at all means the two are not the same drawing.

	bool alpha_channels_match ( const QImage& left, const QImage& right )
	{
		const QImage a = left.convertToFormat  ( QImage::Format_ARGB32 );
		const QImage b = right.convertToFormat ( QImage::Format_ARGB32 );

		if ( a.size () != b.size () )
		{
			return false;
		}

		for ( int y = 0; y < a.height (); ++y )
		{
			for ( int x = 0; x < a.width (); ++x )
			{
				if ( qAlpha ( a.pixel ( x, y ) ) != qAlpha ( b.pixel ( x, y ) ) )
				{
					return false;
				}
			}
		}

		return true;
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

	// has_icon() and available_icons() both answer from ONE master of ONE source, which is only sound while every
	// master carries every name AND both sources carry the same names. The generator refuses to emit a set where the
	// two masters disagree, but nothing upstream compares the SVG tree with the PNG tree -- they are produced by two
	// tools, and the second can simply not have been re-run. This is the same claim checked against what actually
	// shipped, across both sources, so neither a hand-edited asset nor a forgotten export slips past it.

	void every_master_and_both_sources_carry_every_name ()
	{
		QStringList reference;

		for ( const int grid : config::icons::GRIDS )
		{
			const QStringList svgNames =
				resource_base_names ( QStringLiteral ( ":/vje/icons/svg/%1" ).arg ( grid ), QStringLiteral ( ".svg" ) );

			QVERIFY2
			(
				!svgNames.isEmpty (),
				qPrintable ( QStringLiteral ( "no SVG assets compiled in for the %1 master" ).arg ( grid ) )
			);

			if ( reference.isEmpty () )
			{
				reference = svgNames;
			}
			else
			{
				QCOMPARE ( svgNames, reference );
			}

			// The PNG tree carries one directory per RENDERED size, and the rungs above the first are OPTIONAL.
			//
			// The ladder exists to hand QIcon an exact-size render at integer device pixel ratios; a tree carrying only
			// the base size still draws every icon, just scaled at 2x. So an absent rung is reported rather than
			// failed -- but it IS reported, because a silently shortened ladder reads as "fine" until a HiDPI screen
			// says otherwise, and because a rung that exists must still be complete (a HALF-exported size is a genuine
			// fault, and the one a forgotten re-run actually produces).
			//
			// The BASE size is required. Without it the master contributes nothing at its own authored size, which is
			// the one size the whole two-master design exists to get right.

			QStringList absentSizes;

			for ( const int multiple : config::icons::SCALE_MULTIPLES )
			{
				const int pixelSize = grid * multiple;

				const QStringList pngNames = resource_base_names
				(
					QStringLiteral ( ":/vje/icons/png/%1/%2" ).arg ( grid ).arg ( pixelSize ),
					QStringLiteral ( ".png" )
				);

				if ( pngNames.isEmpty () )
				{
					QVERIFY2
					(
						multiple != 1,
						qPrintable ( QStringLiteral ( "the %1 master has no PNG assets at its own size (%2 px) -- run "
						                              "tools/export_icon_pngs" ).arg ( grid ).arg ( pixelSize ) )
					);

					absentSizes.append ( QString::number ( pixelSize ) );

					continue;
				}

				QCOMPARE ( pngNames, reference );
			}

			if ( !absentSizes.isEmpty () )
			{
				qInfo
				(
					"%s",
					qPrintable ( QStringLiteral ( "the %1 master ships only some of its ladder -- %2 px absent, so those "
					                              "device pixel ratios are served by scaling" )
					             .arg ( grid ).arg ( absentSizes.join ( QStringLiteral ( ", " ) ) ) )
				);
			}
		}
	}

	// A DERIVED ASSET CAN GO STALE: whichever tree is not the artwork is produced by a tool that can simply not have
	// been re-run, leaving it drawing the previous version of a glyph -- silently, and in whichever configuration
	// config::icons::SOURCE_FORMAT is not currently serving. This renders every SVG and compares it with the PNG.
	//
	// ONE COMPARISON ANSWERS BOTH DIRECTIONS. Agreement is symmetric, so which tree is the artwork changes only two
	// things: how exactly the two are obliged to match, and which tool the failure tells you to re-run. Keeping it as
	// one case rather than two is what stops the unused direction from rotting -- the arrow reversed once already.
	//
	// Compared by ALPHA rather than by colour, deliberately: the PNGs are alpha masks whose baked tint the application
	// discards (IconLibrary::tint_mask reads coverage alone), so the alpha channel is both the whole of what is used
	// and the one thing a re-export with a different tint would leave unchanged.

	void the_two_trees_are_the_same_artwork ()
	{
		const bool tracedFromRaster = ( config::icons::ARTWORK_SOURCE == config::icons::ArtworkSource::Raster );

		for ( const int grid : config::icons::GRIDS )
		{
			for ( const QString& name : all_icon_name_constants () )
			{
				QFile source ( QStringLiteral ( ":/vje/icons/svg/%1/%2.svg" ).arg ( grid ).arg ( name ) );

				QVERIFY2
				(
					source.open ( QIODevice::ReadOnly ),
					qPrintable ( QStringLiteral ( "no SVG for %1 at grid %2 -- run tools/trace_png_to_svg.py" )
					             .arg ( name ).arg ( grid ) )
				);

				const QByteArray svgText = source.readAll ();

				for ( const int multiple : config::icons::SCALE_MULTIPLES )
				{
					const int pixelSize = grid * multiple;

					const QImage rendered = render_reference ( svgText, pixelSize, !tracedFromRaster );
					const QImage exported (
						QStringLiteral ( ":/vje/icons/png/%1/%2/%3.png" ).arg ( grid ).arg ( pixelSize ).arg ( name ) );

					// A traced SVG owes its exactness to landing on whole pixels, and it owes that to the scale being
					// an integer -- so this is asserted at every rung, INCLUDING the ones the PNG tree does not ship.
					// It is the property the whole transcription rests on, and the only one still checkable where
					// there is nothing to compare against.

					if ( tracedFromRaster )
					{
						QVERIFY2
						(
							partial_coverage_count ( rendered ) == 0,
							qPrintable ( QStringLiteral ( "%1 renders %2 partially-covered pixels at %3 px -- a traced "
							                              "glyph must land on whole pixels at every integer multiple of "
							                              "its grid, so its geometry has left the integer lattice" )
							             .arg ( name ).arg ( partial_coverage_count ( rendered ) ).arg ( pixelSize ) )
						);
					}

					// An absent rung is legal (see every_master_and_both_sources_carry_every_name); what is checked
					// here is that the rungs which DO ship are current.

					if ( exported.isNull () )
					{
						continue;
					}

					QCOMPARE ( exported.size (), rendered.size () );

					QVERIFY2
					(
						alpha_channels_match ( rendered, exported ),
						qPrintable ( QStringLiteral ( "%1 at %2 px differs between the two trees -- re-run %3" )
						             .arg ( name ).arg ( pixelSize )
						             .arg ( tracedFromRaster ? QStringLiteral ( "tools/trace_png_to_svg.py" )
						                                     : QStringLiteral ( "tools/export_icon_pngs" ) ) )
					);
				}
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
	//
	// WHERE THE ARTWORK IS TWO-VALUED THE FLOOR CANNOT FAIL, and a check that cannot fail is worse than no check
	// because it reads like coverage. A two-valued master has a 100% solid core however bad the drawing is. The
	// property worth asserting there is the stronger one it actually promises -- that no delivered pixel carries
	// partial coverage at all -- and since 2026-07-31 that holds through BOTH sources: the PNGs are drawn that way,
	// and the SVGs traced from them rasterize back the same. So the assertion follows ARTWORK_SOURCE rather than
	// SOURCE_FORMAT, and a dropped hardening pass in tools/export_icon_pngs, a mis-traced boundary, or a hand-drawn
	// SVG with a real curve in it each break it.

	void glyphs_are_pixel_crisp_at_every_authored_size ()
	{
		constexpr double SOLID_CORE_FLOOR = 0.35;

		const bool artworkIsTwoValued = ( config::icons::ARTWORK_SOURCE == config::icons::ArtworkSource::Raster );

		for ( const int grid : config::icons::GRIDS )
		{
			long long inked   = 0;
			long long solid   = 0;
			long long partial = 0;

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

						if ( alpha < 255 ) ++partial;
					}
				}
			}

			if ( artworkIsTwoValued )
			{
				QVERIFY2
				(
					partial == 0,
					qPrintable ( QStringLiteral ( "the %1 master delivers %2 partially-covered pixels at %1 px -- the "
					                              "artwork is two-valued so that it can be edited pixel by pixel, and "
					                              "every glyph the application draws from it, through either source, "
					                              "has to arrive that way" ).arg ( grid ).arg ( partial ) )
				);
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

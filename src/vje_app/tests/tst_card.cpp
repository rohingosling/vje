//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Card unit tests (STYLE-01/02/05).
//
//   EVERY CLAIM HERE IS ABOUT RENDERED PIXELS, so every one is read back off a grabbed image rather than asked of the
//   paint code (lesson Q12). The claim that actually needed the machinery -- that a card's corners are rounded even
//   though its content is square -- is invisible to any test that only inspects widgets: the content is a child, Qt
//   paints it after its parent, and a Card whose overlay was missing or left underneath its siblings would report
//   exactly the same geometry, the same palette and the same layout as a correct one while drawing square corners.
//
//   The content widget is deliberately given a colour that is neither the backdrop nor the card surface, so a pixel
//   can be attributed to one of the three without ambiguity.
//
//   Runs under the offscreen QPA platform (set by CTest), so it needs no display.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/Card.hpp"

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"
#include "style/card_surface.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestCard
//*********************************************************************************************************************

class TestCard : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;

	// A colour no theme produces, so a pixel carrying it can only have come from the content widget.

	static QColor content_colour ()
	{
		return QColor ( 255, 0, 255 );
	}

	// The margin framing the card inside the host, so there is backdrop to compare a corner against.

	static constexpr int HOST_MARGIN = 8;

	struct Rendered
	{
		QImage shot;
		QRect  card;
		QRect  content;
		QColor backdrop;
		QColor surface;
		QColor border;
	};

	// A live Card carrying one opaque, square, card-filling child -- which is the shape the corner claim is about.

	static Rendered render_card ( QWidget& host, bool topCornersRounded = true )
	{
		host.setAutoFillBackground ( true );
		host.resize ( 240, 160 );

		Card* const card = new Card ( &host );

		card->set_top_corners_rounded ( topCornersRounded );

		QWidget* const content = new QWidget ( card );

		content->setAutoFillBackground ( true );

		QPalette contentPalette = content->palette ();

		contentPalette.setColor ( QPalette::Window, content_colour () );

		content->setPalette ( contentPalette );

		card->add_content ( content );

		QVBoxLayout* const layout = new QVBoxLayout ( &host );

		layout->setContentsMargins ( HOST_MARGIN, HOST_MARGIN, HOST_MARGIN, HOST_MARGIN );
		layout->addWidget ( card );

		host.show ();

		return Rendered
		{
			host.grab ().toImage (),
			card->geometry (),
			QRect ( content->mapTo ( &host, QPoint ( 0, 0 ) ), content->size () ),
			host.palette ().color ( QPalette::Window ),
			card_surface ( card->palette () ),
			card_border  ( card->palette () )
		};
	}

	// How far two colours are apart, as a plain sum over the channels. Used to attribute a pixel to the nearest of the
	// three known colours rather than demanding an exact match, since the border is drawn antialiased.

	static int distance ( const QColor& left, const QColor& right )
	{
		return qAbs ( left.red   () - right.red   () ) +
		       qAbs ( left.green () - right.green () ) +
		       qAbs ( left.blue  () - right.blue  () );
	}

private slots:

	void init ()
	{
		temporaryDirectory = std::make_unique<QTemporaryDir> ();

		settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
		theme    = std::make_unique<ThemeService>  ( settings.get () );

		theme->apply ();
		theme->set_theme ( Theme::Light );
	}

	void cleanup ()
	{
		// Reverse construction order: ThemeService outlives nothing here, but the rule is the project's (lesson Q1).

		theme.reset    ();
		settings.reset ();

		temporaryDirectory.reset ();
	}

	// STYLE-02, and the claim the overlay exists for: the content is opaque, square and fills the card, so a pixel the
	// content covers reads as the backdrop only if something painted it back AFTER the content was drawn.
	//
	// THE SAMPLE POINT IS THE WHOLE TEST, and the first version of it was worthless. Read at the extreme corner pixel
	// this passes against a build that paints no wedges at all: the content is inset by the border, so nothing covers
	// that pixel in the first place and the host's own background shows through a card that never painted there. One
	// pixel further in, diagonally, is inside the arc AND inside the content -- the only place the two disagree
	// (lesson D10; the neutered run is what found it).

	void the_top_corners_show_the_backdrop_through_square_content ()
	{
		QWidget host;

		const Rendered rendered = render_card ( host );

		// At one pixel in, the 8 px arc is still ~4 px away, so each of these sits well clear of the antialiased edge.

		const QPoint insideTheArc [ 2 ] =
		{
			rendered.card.topLeft  () + QPoint (  1, 1 ),
			rendered.card.topRight () + QPoint ( -1, 1 )
		};

		for ( const QPoint& sample : insideTheArc )
		{
			QVERIFY ( rendered.shot.pixelColor ( sample ) != content_colour () );

			QCOMPARE ( rendered.shot.pixelColor ( sample ), rendered.backdrop );
		}
	}

	// The BOTTOM corners are square, and deliberately so: an item view puts its horizontal scroll bar along the bottom
	// edge, full width, and a fillet cuts the ends of that bar off on the diagonal -- which reads as a rendering fault
	// rather than as a rounded corner. Asserted as the opposite of the case above, so the two cannot both hold and the
	// shape cannot drift back to a uniform radius unnoticed.

	void the_bottom_corners_are_square ()
	{
		QWidget host;

		const Rendered rendered = render_card ( host );

		const QPoint insideTheCorner [ 2 ] =
		{
			rendered.card.bottomLeft  () + QPoint (  1, -1 ),
			rendered.card.bottomRight () + QPoint ( -1, -1 )
		};

		for ( const QPoint& sample : insideTheCorner )
		{
			QVERIFY2
			(
				rendered.shot.pixelColor ( sample ) != rendered.backdrop,
				"A bottom corner was cut away, so the card is still filleted there"
			);
		}

		// And the card reaches its own bottom corner: the outermost pixel is the border, not the backdrop showing
		// through a rounded-off end.

		const QPoint bottomLeftEdge = rendered.card.bottomLeft () + QPoint ( 0, -1 );

		QVERIFY ( distance ( rendered.shot.pixelColor ( bottomLeftEdge ), rendered.border ) <
		          distance ( rendered.shot.pixelColor ( bottomLeftEdge ), rendered.backdrop ) );
	}

	// SET-03. The top corners answer to the setting, so with it OFF they behave exactly as the bottom ones do -- which
	// is asserted as the negation of the case above rather than in its own terms, so the two cannot both pass against an
	// implementation that ignores the setting.

	void the_top_corners_are_square_when_the_setting_is_off ()
	{
		QWidget host;

		const Rendered rendered = render_card ( host, false );

		const QPoint insideTheCorner [ 2 ] =
		{
			rendered.card.topLeft  () + QPoint (  1, 1 ),
			rendered.card.topRight () + QPoint ( -1, 1 )
		};

		for ( const QPoint& sample : insideTheCorner )
		{
			QVERIFY2
			(
				rendered.shot.pixelColor ( sample ) != rendered.backdrop,
				"A top corner was still cut away with Rounded pane corners switched off"
			);
		}

		// And a card built with nobody pushing anything into it starts at the documented default, so the case above is
		// exercising a real switch rather than agreeing with the shape a card would have had anyway.
		//
		// Asserted as a RELATION -- the initial state EQUALS the constant -- and deliberately not as "the constant is
		// true". config::card is a hand-tuning dial, and pinning its value here would make changing your mind about the
		// default fail a painting suite. What the default IS belongs to tst_settings_schema, beside the other stated
		// defaults it guards against drift.

		QWidget defaultHost;

		QCOMPARE ( Card ( &defaultHost ).top_corners_rounded (), config::card::ROUNDED_TOP_CORNERS_DEFAULT );
	}

	// STYLE-02's border, on all four edges. Asserted as "nearer the border colour than to either of the other two"
	// rather than as an exact match, because the stroke is antialiased.

	void the_border_closes_all_four_edges ()
	{
		QWidget host;

		const Rendered rendered = render_card ( host );

		const QPoint midpoints [ 4 ] =
		{
			QPoint ( rendered.card.center ().x (), rendered.card.top    () ),
			QPoint ( rendered.card.center ().x (), rendered.card.bottom () ),
			QPoint ( rendered.card.left   (),      rendered.card.center ().y () ),
			QPoint ( rendered.card.right  (),      rendered.card.center ().y () )
		};

		for ( const QPoint& midpoint : midpoints )
		{
			const QColor pixel = rendered.shot.pixelColor ( midpoint );

			QVERIFY ( distance ( pixel, rendered.border ) < distance ( pixel, rendered.backdrop ) );
			QVERIFY ( distance ( pixel, rendered.border ) < distance ( pixel, rendered.surface  ) );
		}
	}

	// The content is inset by the border, so the ring frames the content instead of being painted across its outermost
	// row -- which is what stops the border reading as a clipped edge of the tree or the grid.

	void the_content_is_inset_by_the_border ()
	{
		QWidget host;

		const Rendered rendered = render_card ( host );

		QCOMPARE ( rendered.content.left (), rendered.card.left () + config::card::BORDER_WIDTH );
		QCOMPARE ( rendered.content.top  (), rendered.card.top  () + config::card::BORDER_WIDTH );
	}

	// STYLE-01. The card's surface is the surface the content is already read on, not a second opinion beside it.

	void the_card_surface_is_the_content_surface ()
	{
		QWidget host;

		QCOMPARE ( card_surface ( host.palette () ), host.palette ().color ( QPalette::Base ).toRgb () );
	}

	// "Subtle" is bounded rather than pinned: the border has to be tellable from the backdrop it closes against AND
	// from the surface it encloses. A border equal to either is not subtle, it is absent.

	void the_border_is_distinguishable_from_both_surfaces ()
	{
		const Theme themes [ 2 ] = { Theme::Light, Theme::Dark };

		for ( const Theme themeChoice : themes )
		{
			theme->set_theme ( themeChoice );

			QWidget host;

			const QColor backdrop = host.palette ().color ( QPalette::Window );
			const QColor border   = card_border ( host.palette () );

			QVERIFY ( distance ( border, backdrop ) > 0 );
			QVERIFY ( distance ( border, card_surface ( host.palette () ) ) > 0 );
		}
	}

	// The tone rule (style/tone.hpp), stated as a DIRECTION as well as a magnitude: the border lifts off a dark
	// backdrop and sinks into a light one. Magnitude alone would pass on a border that had run off the end of the
	// scale and vanished -- which is exactly how the splitter grip was invisible on the light theme for a phase.

	void the_border_lightens_on_dark_and_darkens_on_light ()
	{
		theme->set_theme ( Theme::Dark );

		QWidget darkHost;

		QVERIFY
		(
			card_border ( darkHost.palette () ).lightness () >
			darkHost.palette ().color ( QPalette::Window ).lightness ()
		);

		theme->set_theme ( Theme::Light );

		QWidget lightHost;

		QVERIFY
		(
			card_border ( lightHost.palette () ).lightness () <
			lightHost.palette ().color ( QPalette::Window ).lightness ()
		);
	}
};

QTEST_MAIN ( TestCard )

#include "tst_card.moc"

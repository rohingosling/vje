//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_group_box -- SET-01c's group box: what it draws, and the padding it keeps.
//
//   THE DRAWING IS CHECKED IN RENDERED PIXELS (lesson Q12). The claims are that the outline is ink of one stated colour
//   on both themes, that it BREAKS under the title and nowhere else, and that its corners are rounded or square as asked. A widget
//   inspected rather than rendered satisfies none of those and fails none of them.
//
//   THE PADDING IS CHECKED ACROSS THE TWO EVENTS THAT RESET IT. QGroupBox re-derives its contents margins from the style
//   on a style change and a font change (read from Qt's source); the box re-applies its own after. A case that read the
//   margins once, before either event, would pass against a box that lost them at the first theme change -- which is
//   exactly when a user would see it, since ThemeService installs a new style on every one.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "dialogs/GroupBox.hpp"
#include "services/ThemeService.hpp"
#include "style/dialog_surface.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QAccessible>
#include <QFont>
#include <QImage>
#include <QKeySequence>
#include <QMargins>
#include <QPalette>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

namespace
{
	const QString TITLE = QStringLiteral ( "Indentation" );

	// A box wide enough that its top edge runs well past the title, and tall enough for the corners to be distinct from
	// the edges beside them.

	constexpr int BOX_WIDTH  = 320;
	constexpr int BOX_HEIGHT = 120;
}

//*********************************************************************************************************************
// Class: TestGroupBox
//*********************************************************************************************************************

class TestGroupBox : public QObject
{
	Q_OBJECT

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

private:

	// A shown box at a stated size, built AFTER the theme is set: QApplication::setPalette reaches no widget that already
	// exists (lesson Q9), so a box built first would be drawn in whatever palette it was born with.

	static std::unique_ptr<GroupBox> shown_box ( const QString& title, bool rounded )
	{
		std::unique_ptr<GroupBox> box = std::make_unique<GroupBox> ( title );

		box->set_corners_rounded ( rounded );
		box->resize ( BOX_WIDTH, BOX_HEIGHT );
		box->show ();

		return box;
	}

	// The box drawn onto its own window colour, which is what it sits on in a dialog.

	static QImage rendered ( QWidget& widget )
	{
		QImage image ( widget.size (), QImage::Format_RGB32 );

		image.fill ( widget.palette ().color ( QPalette::Window ) );

		widget.render ( &image );

		return image;
	}

	static bool is_colour ( const QImage& image, int x, int y, const QColor& colour )
	{
		return ( image.pixel ( x, y ) & 0x00FFFFFF ) == ( colour.rgb () & 0x00FFFFFF );
	}

	static QString describe ( const QImage& image, int x, int y )
	{
		return QColor ( image.pixel ( x, y ) ).name () + QStringLiteral ( " at (%1, %2)" ).arg ( x ).arg ( y );
	}

	// The margins the box is meant to keep, from config::group_box and nothing else -- the case restates the rule rather
	// than asking the box for it.

	static QMargins expected_padding ( const GroupBox& box )
	{
		const int side = config::group_box::FRAME_THICKNESS + config::group_box::PADDING;

		return QMargins ( side, box.frame_top () + side, side, side );
	}

	//=================================================================================================================
	// Test Cases
	//=================================================================================================================

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
		// Reverse construction order (lesson Q1).

		theme.reset    ();
		settings.reset ();

		temporaryDirectory.reset ();
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The outline is the frame colour, on BOTH themes, on all four sides -- and the frame colour stands away from the
	// surface in the direction each theme has room for. Checked on both themes because the fault this replaced (Fusion's
	// fixed image) was exactly a line right on one theme and faint on the other.
	//-----------------------------------------------------------------------------------------------------------------

	void the_outline_is_the_frame_colour_on_both_themes_data ()
	{
		QTest::addColumn<bool> ( "dark" );

		QTest::newRow ( "light" ) << false;
		QTest::newRow ( "dark" )  << true;
	}

	void the_outline_is_the_frame_colour_on_both_themes ()
	{
		QFETCH ( bool, dark );

		theme->set_theme ( dark ? Theme::Dark : Theme::Light );

		const std::unique_ptr<GroupBox> box = shown_box ( TITLE, true );

		const QImage image   = rendered ( *box );
		const QColor frame   = dialog_group_frame ( box->palette () );
		const QColor surface = box->palette ().color ( QPalette::Window );

		const int middleX = BOX_WIDTH / 2;
		const int middleY = ( box->frame_top () + BOX_HEIGHT ) / 2;

		QVERIFY2 ( is_colour ( image, 0,             middleY,        frame ), qPrintable ( describe ( image, 0, middleY ) ) );
		QVERIFY2 ( is_colour ( image, BOX_WIDTH - 1, middleY,        frame ), qPrintable ( describe ( image, BOX_WIDTH - 1, middleY ) ) );
		QVERIFY2 ( is_colour ( image, middleX,       BOX_HEIGHT - 1, frame ), qPrintable ( describe ( image, middleX, BOX_HEIGHT - 1 ) ) );

		// The top edge runs through the title's centre line, well to the right of the title.

		QVERIFY2
		(
			is_colour ( image, BOX_WIDTH - 40, box->frame_top (), frame ),
			qPrintable ( describe ( image, BOX_WIDTH - 40, box->frame_top () ) )
		);

		// The direction the theme has room for: darker than a light surface, lighter than a dark one. Asserted as
		// opposites, so a colour that ignored the theme would fail one of the two rows.

		if ( dark )
		{
			QVERIFY ( frame.lightness () > surface.lightness () );
		}
		else
		{
			QVERIFY ( frame.lightness () < surface.lightness () );
		}

		// And the interior is the surface -- the box outlines its content, it does not fill it.

		QVERIFY2 ( is_colour ( image, middleX, middleY, surface ), qPrintable ( describe ( image, middleX, middleY ) ) );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The title sits IN the top edge: the line stops TITLE_GAP short of it on each side, and runs up to that point. The
	// pixels checked are the gap's own, between the line's end and the text, so no glyph can be what the case reads.
	//
	// Square-cornered, so the line before the break is straight: under a rounded corner the arc runs to within half a
	// pixel of the break (config::group_box's static_assert), and a pixel on an arc is not a whole pixel of ink.
	//-----------------------------------------------------------------------------------------------------------------

	void the_outline_breaks_for_the_title ()
	{
		const std::unique_ptr<GroupBox> box = shown_box ( TITLE, false );

		const QImage image   = rendered ( *box );
		const QColor frame   = dialog_group_frame ( box->palette () );
		const QColor surface = box->palette ().color ( QPalette::Window );

		const QRect title = box->title_rect ();
		const int   line  = box->frame_top ();
		const int   gap   = config::group_box::TITLE_GAP;

		QVERIFY ( !title.isEmpty () );
		QVERIFY ( line > 0 );

		// Inside the break, either side of the text: the surface.

		for ( int x = title.left () - gap; x < title.left (); ++x )
		{
			QVERIFY2 ( is_colour ( image, x, line, surface ), qPrintable ( describe ( image, x, line ) ) );
		}

		for ( int x = title.right () + 1; x <= title.right () + gap; ++x )
		{
			QVERIFY2 ( is_colour ( image, x, line, surface ), qPrintable ( describe ( image, x, line ) ) );
		}

		// Just outside it: the line.

		const int beforeBreak = title.left () - gap - 2;
		const int afterBreak  = title.right () + gap + 2;

		QVERIFY2 ( is_colour ( image, beforeBreak, line, frame ), qPrintable ( describe ( image, beforeBreak, line ) ) );
		QVERIFY2 ( is_colour ( image, afterBreak,  line, frame ), qPrintable ( describe ( image, afterBreak, line ) ) );

		// The title's text lines up with the box's content, which is what makes it read as the column's heading.

		QCOMPARE ( title.left (), box->contentsMargins ().left () );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Rounded corners (the default, under both interface styles) or square ones on request. The corner pixel itself is the discriminator -- inked
	// where the corner is square, the bare surface where an arc has cut across it.
	//-----------------------------------------------------------------------------------------------------------------

	void the_corners_follow_the_interface_style ()
	{
		const std::unique_ptr<GroupBox> rounded = shown_box ( TITLE, true );
		const std::unique_ptr<GroupBox> square  = shown_box ( TITLE, false );

		QVERIFY (  rounded->corners_rounded () );
		QVERIFY ( !square->corners_rounded () );

		const QImage roundedImage = rendered ( *rounded );
		const QImage squareImage  = rendered ( *square );

		const QColor frame   = dialog_group_frame ( rounded->palette () );
		const QColor surface = rounded->palette ().color ( QPalette::Window );

		const int top    = rounded->frame_top ();
		const int bottom = BOX_HEIGHT - 1;
		const int right  = BOX_WIDTH - 1;

		const QList<QPoint> corners = { QPoint ( 0, top ), QPoint ( right, top ), QPoint ( 0, bottom ), QPoint ( right, bottom ) };

		// Every corner is checked before anything is reported, so a failure names all the corners that are wrong rather
		// than the first -- which corner fails is itself the diagnosis.

		QStringList wrong;

		for ( const QPoint& corner : corners )
		{
			if ( !is_colour ( roundedImage, corner.x (), corner.y (), surface ) )
			{
				wrong.append ( QStringLiteral ( "rounded: " ) + describe ( roundedImage, corner.x (), corner.y () ) );
			}

			if ( !is_colour ( squareImage, corner.x (), corner.y (), frame ) )
			{
				wrong.append ( QStringLiteral ( "square: " ) + describe ( squareImage, corner.x (), corner.y () ) );
			}
		}

		QVERIFY2 ( wrong.isEmpty (), qPrintable ( wrong.join ( QStringLiteral ( "; " ) ) ) );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// A title is plain text. An ampersand followed by a space is a mnemonic to Qt -- Alt+Space, the window's system menu
	// -- and would be drawn as an underlined space, so the box escapes it for Qt and draws the title as given.
	//-----------------------------------------------------------------------------------------------------------------

	void an_ampersand_in_a_title_is_text_not_a_mnemonic ()
	{
		const QString plain = QStringLiteral ( "Theme & Style" );

		const std::unique_ptr<GroupBox> box = shown_box ( plain, true );

		QCOMPARE ( box->plain_title (), plain );

		// No shortcut is registered for the title: QGroupBox grabs QKeySequence::mnemonic ( title () ).

		QVERIFY2 ( QKeySequence::mnemonic ( box->title () ).isEmpty (), qPrintable ( box->title () ) );

		// And the unescaped title is exactly what the case is guarding against: Alt+Space.

		QCOMPARE ( QKeySequence::mnemonic ( plain ), QKeySequence ( Qt::ALT | Qt::Key_Space ) );

		// Assistive technology reads the title as written (NFR-05).

		QAccessibleInterface* const accessible = QAccessible::queryAccessibleInterface ( box.get () );

		QVERIFY ( accessible != nullptr );
		QCOMPARE ( accessible->text ( QAccessible::Name ), plain );
		QCOMPARE ( static_cast<int> ( accessible->role () ), static_cast<int> ( QAccessible::Grouping ) );

		// The text is DRAWN in full: ink reaches the right-hand end of the title's advance. Drawn as a mnemonic, the
		// ampersand would vanish and the ink would stop an ampersand's width short.

		const QImage image   = rendered ( *box );
		const QColor surface = box->palette ().color ( QPalette::Window );
		const QRect  title   = box->title_rect ();

		int rightmostInk = -1;

		for ( int x = title.left (); x <= title.right (); ++x )
		{
			for ( int y = title.top (); y <= title.bottom (); ++y )
			{
				if ( ( y != box->frame_top () ) && !is_colour ( image, x, y, surface ) )
				{
					rightmostInk = x;
				}
			}
		}

		QVERIFY2
		(
			rightmostInk >= ( title.right () - 2 ),
			qPrintable ( QStringLiteral ( "ink ends at %1, title ends at %2" ).arg ( rightmostInk ).arg ( title.right () ) )
		);
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The padding is config::group_box's, and it SURVIVES the two events on which QGroupBox re-derives its own from the
	// style: a style change (ThemeService installs a new style on every apply) and a font change.
	//-----------------------------------------------------------------------------------------------------------------

	void the_padding_survives_a_style_change_and_a_font_change ()
	{
		const std::unique_ptr<GroupBox> box = shown_box ( TITLE, true );

		QCOMPARE ( box->contentsMargins (), expected_padding ( *box ) );

		// The top is measured from the line the title sits on, which is the title's vertical centre.

		QFont bold = box->font ();

		bold.setBold ( true );

		QCOMPARE ( box->frame_top (), QFontMetrics ( bold ).height () / 2 );

		// A new application style -- the real event, the one a theme change sends.

		theme->apply ();

		QCOMPARE ( box->contentsMargins (), expected_padding ( *box ) );

		// A larger font moves the title's centre line down, and the padding with it.

		const int lineBefore = box->frame_top ();

		QFont larger = box->font ();

		larger.setPointSizeF ( larger.pointSizeF () * 2.0 );

		box->setFont ( larger );

		QVERIFY ( box->frame_top () > lineBefore );
		QCOMPARE ( box->contentsMargins (), expected_padding ( *box ) );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// A box with no title is a closed outline: nothing to break for, and the top edge along the top.
	//-----------------------------------------------------------------------------------------------------------------

	void a_box_without_a_title_is_a_closed_outline ()
	{
		const std::unique_ptr<GroupBox> box = shown_box ( QString (), true );

		QCOMPARE ( box->frame_top (), 0 );
		QVERIFY  ( box->title_rect ().isEmpty () );

		const QImage image = rendered ( *box );
		const QColor frame = dialog_group_frame ( box->palette () );

		for ( int x = 20; x < BOX_WIDTH - 20; x += 10 )
		{
			QVERIFY2 ( is_colour ( image, x, 0, frame ), qPrintable ( describe ( image, x, 0 ) ) );
		}
	}

	//=================================================================================================================
	// Data Members
	//=================================================================================================================

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;
};

QTEST_MAIN ( TestGroupBox )

#include "tst_group_box.moc"

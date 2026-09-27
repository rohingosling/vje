//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_dialog_frame -- STYLE-15's four parts, and the two colours they are drawn in.
//
//   THE RULE IS CHECKED IN RENDERED PIXELS, which is this project's rule for any claim about painting (lesson Q12,
//   and printing/page_furniture's precedent): a divider is not "a widget in the layout", it is a line of ink spanning
//   the dialog, and a frame that adds a zero-height widget or an inset one satisfies the first description while
//   failing the second. Counting rows inked EDGE TO EDGE is what tells those apart -- nothing else in a dialog spans
//   its full width, so the count is exact rather than approximate.
//
//   THE TWO COLOUR CLAIMS ARE WRITTEN AS OPPOSITES, for the reason the FluentStyle cases are: a single-direction
//   assertion passes against an implementation that ignores the palette in the other direction. The dimmed prose is
//   bounded from BOTH ends -- quieter than ordinary text, and still clearing WCAG AA -- because either bound alone is
//   satisfied by a colour that is obviously wrong.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "dialogs/dialog_frame.hpp"
#include "services/ThemeService.hpp"
#include "style/dialog_surface.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestDialogFrame
//*********************************************************************************************************************

class TestDialogFrame : public QObject
{
	Q_OBJECT

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

private:

	// A dialog carrying the frame, sized so there is room for all three parts. Content is a labelled placeholder
	// rather than an empty widget, so a rendered page is never blank -- an all-blank page would let "no rule" pass a
	// test looking only for the absence of ink.

	static QDialog* framed_dialog ( QWidget* parent, const QIcon& icon = QIcon () )
	{
		QDialog* const dialog = new QDialog ( parent );

		QWidget* const content = new QWidget ( dialog );

		QVBoxLayout* const contentLayout = new QVBoxLayout ( content );

		contentLayout->setContentsMargins ( 0, 0, 0, 0 );
		contentLayout->addWidget ( new QLabel ( QStringLiteral ( "Content." ), content ) );

		QDialogButtonBox* const buttons = new QDialogButtonBox
		(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
			dialog
		);

		apply_dialog_frame ( *dialog, content, buttons, icon );

		dialog->resize ( 400, 240 );
		dialog->show ();

		// Offscreen still lays out; the layout has to be activated before any geometry is worth reading.

		dialog->layout ()->activate ();

		return dialog;
	}

	// Rows in which EVERY pixel is the given colour. A hairline spanning the dialog is the only thing that produces
	// one, which is what makes the count a measurement of the rule rather than of anything else on the page.

	static int rows_inked_edge_to_edge ( const QImage& image, const QColor& ink )
	{
		int rows = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			bool wholeRow = true;

			for ( int x = 0; ( x < image.width () ) && wholeRow; ++x )
			{
				wholeRow = ( ( image.pixel ( x, y ) & 0x00FFFFFF ) == ( ink.rgb () & 0x00FFFFFF ) );
			}

			rows += wholeRow ? 1 : 0;
		}

		return rows;
	}

	static QImage rendered ( QWidget& widget )
	{
		QImage image ( widget.size (), QImage::Format_RGB32 );

		image.fill ( widget.palette ().color ( QPalette::Window ) );

		widget.render ( &image );

		return image;
	}

	// WCAG 2.x relative luminance and contrast ratio, so the accessibility claim is a measurement rather than a
	// judgement about a lightness number.

	static double channel_luminance ( int component )
	{
		const double value = component / 255.0;

		return ( value <= 0.03928 ) ? ( value / 12.92 ) : std::pow ( ( value + 0.055 ) / 1.055, 2.4 );
	}

	static double relative_luminance ( const QColor& colour )
	{
		return 0.2126 * channel_luminance ( colour.red   () )
		     + 0.7152 * channel_luminance ( colour.green () )
		     + 0.0722 * channel_luminance ( colour.blue  () );
	}

	static double contrast_ratio ( const QColor& left, const QColor& right )
	{
		const double a = relative_luminance ( left );
		const double b = relative_luminance ( right );

		return ( qMax ( a, b ) + 0.05 ) / ( qMin ( a, b ) + 0.05 );
	}

	static int lightness_distance ( const QColor& left, const QColor& right )
	{
		return qAbs ( left.lightness () - right.lightness () );
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

	// STYLE-15's ORDER, asserted geometrically rather than by walking the layout: what the requirement promises is
	// that the rule sits between the content and the buttons on screen, and a layout holding three items in the right
	// sequence can still stack them wrongly if any of them is given the wrong stretch or alignment.

	void the_four_parts_are_present_and_in_order ()
	{
		QWidget host;

		std::unique_ptr<QDialog> dialog ( framed_dialog ( &host ) );

		QLabel* const content = dialog->findChild<QLabel*> ();

		QDialogButtonBox* const buttons = dialog->findChild<QDialogButtonBox*> ();

		QVERIFY ( content != nullptr );
		QVERIFY ( buttons != nullptr );

		// The rule is the one child that is neither of those and neither's descendant -- deliberately not found by an
		// object name, because a name is bookkeeping the frame could satisfy while painting nothing.

		QWidget* rule = nullptr;

		for ( QWidget* const child : dialog->findChildren<QWidget*> ( QString (), Qt::FindDirectChildrenOnly ) )
		{
			if ( ( child != buttons ) && ( child->findChildren<QLabel*> ().isEmpty () ) && ( child != content ) )
			{
				if ( child->height () == config::dialog::RULE_THICKNESS )
				{
					rule = child;
				}
			}
		}

		QVERIFY2 ( rule != nullptr, "no one-pixel-high divider between the content and the buttons" );

		const QRect contentRect = content->geometry ().translated ( content->parentWidget ()->pos () );

		QVERIFY2 ( contentRect.bottom () <= rule->y (), "the rule is not below the content" );
		QVERIFY2 ( rule->geometry ().bottom () <= buttons->y (), "the buttons are not below the rule" );
	}

	// The rule spans the dialog EDGE TO EDGE. Fails against a frame that adds no rule (zero such rows) and against one
	// that insets it to the content margin (also zero), which are the two ways this goes wrong.

	void the_rule_is_one_row_inked_across_the_whole_dialog ()
	{
		QWidget host;

		std::unique_ptr<QDialog> dialog ( framed_dialog ( &host ) );

		const QImage page = rendered ( *dialog );

		QCOMPARE ( rows_inked_edge_to_edge ( page, dialog_rule ( dialog->palette () ) ),
		           config::dialog::RULE_THICKNESS );

		// And the page is not blank, so "no rule" cannot pass by there being nothing on it to compare against: the
		// surface itself covers rows that are emphatically NOT the rule colour.

		QVERIFY ( rows_inked_edge_to_edge ( page, dialog->palette ().color ( QPalette::Window ) ) > 0 );
	}

	// The row is right-aligned. Asserted on the RIGHTMOST button rather than on the accept button, because which of
	// OK and Cancel ends up on the right is the platform's call -- QDialogButtonBox reads QStyle::SH_DialogButtonLayout
	// and Windows puts OK first -- and honouring that is the whole reason for using the box. What STYLE-15 requires is
	// that the group ends at the right edge, whichever button that turns out to be.
	//
	// Written against the DIALOG's width rather than the box's, because a box sitting on the left would satisfy a
	// box-relative assertion perfectly while failing the requirement.

	void the_button_row_is_right_aligned ()
	{
		QWidget host;

		std::unique_ptr<QDialog> dialog ( framed_dialog ( &host ) );

		QDialogButtonBox* const buttons = dialog->findChild<QDialogButtonBox*> ();

		QVERIFY ( buttons != nullptr );

		int rightmost = 0;
		int leftmost  = dialog->width ();

		for ( QAbstractButton* const button : buttons->buttons () )
		{
			rightmost = qMax ( rightmost, buttons->x () + button->geometry ().right () );
			leftmost  = qMin ( leftmost,  buttons->x () + button->geometry ().left  () );
		}

		QVERIFY ( !buttons->buttons ().isEmpty () );

		// Within the content margin of the dialog's right edge, allowing for the box's own style margin.

		QVERIFY2
		(
			( dialog->width () - rightmost ) <= ( config::dialog::CONTENT_MARGIN + BUTTON_BOX_MARGIN_TOLERANCE ),
			qPrintable ( QStringLiteral ( "rightmost button ends at %1 in a dialog %2 wide" )
			             .arg ( rightmost ).arg ( dialog->width () ) )
		);

		// ...and there is slack on the LEFT, so a row of buttons stretched across the whole width cannot pass by
		// having its right edge in the right place.

		QVERIFY2
		(
			leftmost > ( dialog->width () / 2 ),
			qPrintable ( QStringLiteral ( "leftmost button starts at %1 in a dialog %2 wide" )
			             .arg ( leftmost ).arg ( dialog->width () ) )
		);
	}

	// The title-bar glyph is the frame's to set, not a call the caller makes afterwards -- which is the whole reason
	// it is a parameter (dialog_frame.hpp).

	void the_frame_sets_the_dialogs_own_icon ()
	{
		QWidget host;

		QPixmap pixmap ( 16, 16 );

		pixmap.fill ( Qt::red );

		std::unique_ptr<QDialog> dialog ( framed_dialog ( &host, QIcon ( pixmap ) ) );

		QVERIFY2 ( !dialog->windowIcon ().isNull (), "the dialog carries no window icon" );

		QCOMPARE ( dialog->windowIcon ().pixmap ( 16, 16 ).toImage (), pixmap.toImage () );
	}

	// The frame absorbs the context-help button every dialog was stripping for itself -- and SettingsDialog was not.
	//
	// THE HINT IS SET DELIBERATELY FIRST, and that is the whole case. Written the obvious way -- construct a dialog,
	// assert the flag is clear -- it passes against a frame that never touches it, because Qt 6 does not add the hint
	// by default on this platform in the first place. The neutered run is what found that (D20): the assertion was
	// green against a build with the clearing line deleted. Setting it up front is what makes the case about the
	// frame's behaviour rather than about the toolkit's default.

	void the_frame_removes_the_context_help_button ()
	{
		QWidget host;

		QDialog dialog ( &host );

		dialog.setWindowFlags ( dialog.windowFlags () | Qt::WindowContextHelpButtonHint );

		QVERIFY2
		(
			( dialog.windowFlags () & Qt::WindowContextHelpButtonHint ) != Qt::WindowFlags (),
			"the fixture could not set the hint, so the case would prove nothing"
		);

		QWidget* const content = new QWidget ( &dialog );

		QDialogButtonBox* const buttons = new QDialogButtonBox ( QDialogButtonBox::Close, &dialog );

		apply_dialog_frame ( dialog, content, buttons, QIcon () );

		QVERIFY ( ( dialog.windowFlags () & Qt::WindowContextHelpButtonHint ) == Qt::WindowFlags () );
	}

	// The rule is a DISTANCE from the surface, so it moves with the theme and is visible against both. Written as
	// opposites: a hard-coded colour passes one half and fails the other, whichever half it was picked for.

	void the_rule_answers_to_the_theme_in_both_directions ()
	{
		// A WIDGET PER THEME, and that is not tidiness. QApplication::setPalette reaches no already-constructed widget
		// on Qt 6.10 (lesson Q9), so a host built once and read twice reports the palette it was born under for both
		// halves -- which is exactly how this case failed the first time it was run.

		theme->set_theme ( Theme::Light );

		const QWidget lightHost;

		const QColor lightSurface = lightHost.palette ().color ( QPalette::Window );
		const QColor lightRule    = dialog_rule ( lightHost.palette () );

		theme->set_theme ( Theme::Dark );

		const QWidget darkHost;

		const QColor darkSurface = darkHost.palette ().color ( QPalette::Window );
		const QColor darkRule    = dialog_rule ( darkHost.palette () );

		// Visible against each, by the stated distance.

		QCOMPARE ( lightness_distance ( lightRule, lightSurface ), config::dialog::RULE_CONTRAST );
		QCOMPARE ( lightness_distance ( darkRule,  darkSurface  ), config::dialog::RULE_CONTRAST );

		// And it moves in OPPOSITE directions, which is the half a fixed offset would fail: the light rule is darker
		// than its surface, the dark rule lighter than its own.

		QVERIFY2 ( lightRule.lightness () < lightSurface.lightness (), "the light theme's rule is not darker than its surface" );
		QVERIFY2 ( darkRule.lightness  () > darkSurface.lightness  (), "the dark theme's rule is not lighter than its surface" );
	}

	// STYLE-15's prose rule, bounded from both ends. The AA floor is the half that matters: the containment rule the
	// spec's section 5 exception was accepted with is that PlaceholderText is for placeholders, and prose gets a tone
	// that can actually be read.

	void dimmed_prose_is_quieter_than_text_and_still_clears_wcag_aa ()
	{
		const Theme themes [ 2 ] = { Theme::Light, Theme::Dark };

		for ( const Theme scheme : themes )
		{
			theme->set_theme ( scheme );

			// Constructed INSIDE the loop, for the reason the rule case states: a widget built before the theme change
			// keeps the palette it was born under, and the second iteration would silently re-check the first.

			const QWidget host;

			const QColor surface = host.palette ().color ( QPalette::Window );
			const QColor text    = host.palette ().color ( QPalette::WindowText );
			const QColor prose   = dialog_dimmed_prose ( host.palette () );

			// Quieter than ordinary text: nearer the surface it sits on.

			QVERIFY2
			(
				lightness_distance ( prose, surface ) < lightness_distance ( text, surface ),
				"the dimmed prose is not quieter than ordinary text"
			);

			// ...and still readable. 4.5:1 is WCAG AA for body text.

			const double ratio = contrast_ratio ( prose, surface );

			QVERIFY2
			(
				ratio >= 4.5,
				qPrintable ( QStringLiteral ( "dimmed prose contrast is %1:1 against its surface" ).arg ( ratio ) )
			);

			// It is emphatically NOT the placeholder role, whose light value is an accepted sub-AA exception. If the
			// two ever coincide, the containment rule has quietly lapsed.

			QVERIFY2
			(
				prose.rgb () != host.palette ().color ( QPalette::PlaceholderText ).rgb (),
				"the dimmed prose tone has collapsed onto QPalette::PlaceholderText"
			);
		}
	}

	//=================================================================================================================
	// Data Members
	//=================================================================================================================

private:

	// A QDialogButtonBox carries its own contents margin from the style, so the accept button's right edge sits a few
	// pixels inside the row rather than exactly on the frame's margin.

	static constexpr int BUTTON_BOX_MARGIN_TOLERANCE = 12;

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;
};

QTEST_MAIN ( TestDialogFrame )

#include "tst_dialog_frame.moc"

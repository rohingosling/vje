//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_message_box -- STYLE-18's one structure for every message box, and LD-9's Windows button order.
//
//   EVERY SIZE AND POSITION IS READ FROM A BOX THAT HAS BEEN SHOWN. The claims are about what QMessageBox's own sizing
//   does to the box -- it measures its layout and fixes its own size in showEvent -- so a box inspected before it is
//   shown answers a different question. Each case that asserts the floor is paired with a guard showing the SAME
//   content in a plain QMessageBox opens smaller, so the floor case cannot pass because the content happened to be
//   big enough on its own (lesson D20).
//
//   THE REBUILD CASES ARE THE ONES THE DESIGN EXISTS FOR. QMessageBox replaces its grid on setInformativeText and on a
//   style change (measured, architecture.md section 8.4), so a floor applied at construction, or applied once at show
//   and never again, passes every other case here and fails exactly these.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "dialogs/MessageBox.hpp"
#include "style/dialog_surface.hpp"
#include "style/FluentStyle.hpp"

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QMessageBox>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleFactory>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <memory>

using namespace vje;

namespace
{
	//-----------------------------------------------------------------------------------------------------------------
	// A base style answering as a GNOME desktop's platform theme would: GnomeLayout, centred message-box buttons. It is
	// what FluentStyle has to override, and it stands in for the Linux desktop a Windows host cannot provide.
	//-----------------------------------------------------------------------------------------------------------------

	class GnomeLikeStyle : public QProxyStyle
	{
	public:

		explicit GnomeLikeStyle ( QStyle* baseStyle )
		:	QProxyStyle ( baseStyle )
		{
		}

		int styleHint ( StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData ) const override
		{
			switch ( hint )
			{
				case SH_DialogButtonLayout:       return QDialogButtonBox::GnomeLayout;
				case SH_MessageBox_CenterButtons: return 1;

				default: break;
			}

			return QProxyStyle::styleHint ( hint, option, widget, returnData );
		}
	};

	const QString SHORT_TEXT = QStringLiteral ( "Printing failed." );

	// Long enough to wrap, with the kind of long unbroken token FILE-06's error really carries -- a path. Text alone
	// cannot widen a box past the floor on this platform: Qt wraps at half the screen's width (400 of the offscreen
	// screen's 800, measured), and breaks even this path to do it. long_box_grows_past_the_floor says what does.

	const QString LONG_TEXT = QStringLiteral
	(
		"Could not open \"C:/Users/someone/Documents/projects/an-unusually-long-folder-name/settings-for-the-release.json\". "
		"The file is not valid JSON."
	);

	QSize floor_size ()
	{
		return QSize ( config::message_box::MINIMUM_WIDTH, config::message_box::MINIMUM_HEIGHT );
	}

	// Shows the box and waits until Qt has sized it and laid it out.

	void show_and_settle ( QMessageBox& box )
	{
		box.show ();

		QVERIFY ( QTest::qWaitForWindowExposed ( &box ) );

		box.layout ()->activate ();

		QCoreApplication::processEvents ();
	}

	// The box's buttons, left to right, by caption.

	QStringList captions_left_to_right ( const QMessageBox& box )
	{
		QList<QAbstractButton*> buttons = box.buttons ();

		std::sort ( buttons.begin (), buttons.end (), [] ( QAbstractButton* left, QAbstractButton* right ) { return left->x () < right->x (); } );

		QStringList captions;

		for ( const QAbstractButton* const button : buttons )
		{
			captions.append ( button->text () );
		}

		return captions;
	}

	QLabel* message_label ( const QMessageBox& box )
	{
		return box.findChild<QLabel*> ( QStringLiteral ( "qt_msgbox_label" ) );
	}

	QLabel* icon_label ( const QMessageBox& box )
	{
		return box.findChild<QLabel*> ( QStringLiteral ( "qt_msgboxex_icon_label" ) );
	}

	void install_application_style ()
	{
		QApplication::setStyle ( new FluentStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ) );
	}

	// The bounding box of the text actually painted inside a region of the box: every pixel that differs clearly from
	// the box's surface. Alignment is a claim about where text is DRAWN, so it is read from a grab rather than from the
	// label's flags or geometry (lesson Q12) -- a label can carry the right flags and fill its row while its text sits
	// anywhere in it.

	QRect ink_rect ( MessageBox& box, const QRect& region )
	{
		const QImage image      = box.grab ().toImage ();
		const QColor background = box.palette ().color ( QPalette::Window );

		const qreal scale = image.devicePixelRatio ();

		QRect ink;

		for ( int y = region.top (); y <= region.bottom (); ++y )
		{
			for ( int x = region.left (); x <= region.right (); ++x )
			{
				const QColor pixel = image.pixelColor ( qRound ( x * scale ), qRound ( y * scale ) );

				const int difference = std::abs ( pixel.red   () - background.red   () )
				                     + std::abs ( pixel.green () - background.green () )
				                     + std::abs ( pixel.blue  () - background.blue  () );

				if ( difference > 90 )
				{
					ink = ink.united ( QRect ( x, y, 1, 1 ) );
				}
			}
		}

		return ink;
	}

	// The rows of a dialog inked EDGE TO EDGE in the rule's colour -- tst_dialog_frame's measure of the framed dialogs'
	// rule, applied here: nothing else in a dialog spans its whole width in that one colour, so the count is exact, and
	// a rule short of either edge is not counted at all.

	QList<int> rule_rows ( QWidget& dialog )
	{
		const QImage image = dialog.grab ().toImage ();
		const QRgb   rule  = dialog_rule ( dialog.palette () ).rgb ();
		const qreal  scale = image.devicePixelRatio ();

		QList<int> rows;

		for ( int y = 0; y < dialog.height (); ++y )
		{
			bool inked = true;

			for ( int x = 0; ( x < dialog.width () ) && inked; ++x )
			{
				inked = ( image.pixel ( qRound ( x * scale ), qRound ( y * scale ) ) & 0xFFFFFFu ) == ( rule & 0xFFFFFFu );
			}

			if ( inked )
			{
				rows.append ( y );
			}
		}

		return rows;
	}

	// The area the text is aligned within: from the top inset down to the button row, across the text column. Qt's
	// message-box grid leaves NO gap above the buttons -- measured, the text rows end at y = 116 and the button box starts
	// at 117 -- so the area ends where the buttons begin. The icon's column is left out, so its ink never reads as text.

	QRect text_area ( MessageBox& box )
	{
		const QDialogButtonBox* const buttons = box.findChild<QDialogButtonBox*> ();

		const int left   = message_label ( box )->x ();
		const int top    = config::dialog::CONTENT_MARGIN;
		const int right  = box.width () - config::dialog::CONTENT_MARGIN - 1;
		const int bottom = buttons->y () - 1;

		return QRect ( QPoint ( left, top ), QPoint ( right, bottom ) );
	}
}

//*********************************************************************************************************************
// Class: TestMessageBox
//*********************************************************************************************************************

class TestMessageBox : public QObject
{
	Q_OBJECT

private slots:

	// Every case starts under the application's own style, since the Windows-order case replaces it.

	void init ()
	{
		install_application_style ();
	}

	//=================================================================================================================
	// (1) The size floor
	//=================================================================================================================

	void short_box_opens_at_the_floor ()
	{
		// Guard: the same content, unfloored, opens smaller in BOTH dimensions -- so the floor is what the next
		// assertion measures, not the content.

		QMessageBox plain ( QMessageBox::Critical, QStringLiteral ( "Print" ), SHORT_TEXT, QMessageBox::Ok );

		show_and_settle ( plain );

		QVERIFY2 ( plain.width  () < floor_size ().width  (), "the guard needs a message narrower than the floor" );
		QVERIFY2 ( plain.height () < floor_size ().height (), "the guard needs a message shorter than the floor" );

		MessageBox box ( MessageKind::Error, QStringLiteral ( "Print" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		QCOMPARE ( box.size (), floor_size () );
	}

	void long_box_grows_past_the_floor ()
	{
		// A floor, never a fixed size: content wider than the floor widens the box past it, and the wrapped text is not
		// clipped.
		//
		// The width comes from the BUTTON ROW rather than the text. Qt wraps a message at half the screen's width -- 400
		// here, the floor's own width -- and breaks even an unbreakable token to do it (measured: the long path above
		// still opened at exactly 400). Six buttons are wider than 400, and they widen the box through the same grid
		// minimum the floor's spacer sits in, which is the mechanism this case is about.

		const QMessageBox::StandardButtons sixButtons = QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
		                                              | QMessageBox::Yes  | QMessageBox::No      | QMessageBox::Abort;

		MessageBox box ( MessageKind::Warning, QStringLiteral ( "Paste" ), LONG_TEXT, sixButtons, QMessageBox::Cancel, QMessageBox::Cancel );

		show_and_settle ( box );

		QVERIFY2 ( box.width () > floor_size ().width (), qPrintable ( QStringLiteral ( "width %1" ).arg ( box.width () ) ) );
		QVERIFY  ( box.height () >= floor_size ().height () );

		const QLabel* const label = message_label ( box );

		// heightForWidth at the width the label actually got, not sizeHint: a word-wrapped label's size hint is taken
		// at a narrower width of Qt's own choosing, so it is taller than the text needs here and the comparison would
		// fail against a correct box.

		QVERIFY ( label != nullptr );
		QVERIFY ( label->wordWrap () );
		QVERIFY2
		(
			label->height () >= label->heightForWidth ( label->width () ),
			qPrintable ( QStringLiteral ( "label %1 x %2, needs %3" ).arg ( label->width () ).arg ( label->height () ).arg ( label->heightForWidth ( label->width () ) ) )
		);
	}

	void informative_text_added_after_construction_keeps_the_floor ()
	{
		// setInformativeText REBUILDS the grid (measured). A floor applied at construction is deleted with the old
		// grid; applied at show, it is in the grid Qt measures.

		MessageBox box ( MessageKind::Warning, QStringLiteral ( "VJE" ), QStringLiteral ( "Save changes?" ), QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		box.setInformativeText ( QStringLiteral ( "Later." ) );

		// Read the size the moment show() returns, BEFORE the event loop runs. changeEvent re-applies a lost floor on
		// the next change event, and showing a window delivers one (its activation) -- so a box floored only by that
		// safety net still settles at the floor, one visible resize late. This is what tells the two apart: only a
		// floor applied in showEvent is there when Qt first sizes the box.

		box.show ();

		QCOMPARE ( box.size (), floor_size () );

		show_and_settle ( box );

		QCOMPARE ( box.size (), floor_size () );
	}

	void style_change_while_open_keeps_the_floor_and_the_captions ()
	{
		// ThemeService::apply() installs a new style object every time, so an OS light/dark switch under the System
		// theme restyles an open box. The restyle rebuilt the grid and shrank the box to its natural size (measured).

		MessageBox box ( MessageKind::Error, QStringLiteral ( "Print" ), SHORT_TEXT, QMessageBox::Ok | QMessageBox::Discard, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		QCOMPARE ( box.size (), floor_size () );

		install_application_style ();

		QCoreApplication::processEvents ();

		box.layout ()->activate ();

		QCOMPARE ( box.size (), floor_size () );
		QCOMPARE ( box.button ( QMessageBox::Discard )->text (), QStringLiteral ( "Do&n't Save" ) );
	}

	//=================================================================================================================
	// (2) The inset, and where the text sits
	//=================================================================================================================

	void a_rule_divides_the_buttons_from_the_content_as_in_every_dialog ()
	{
		MessageBox box ( MessageKind::Warning, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::No );

		show_and_settle ( box );

		const QDialogButtonBox* const buttons = box.findChild<QDialogButtonBox*> ();

		QVERIFY ( buttons != nullptr );

		// One line, one pixel, edge to edge.

		const QList<int> rows = rule_rows ( box );

		QCOMPARE ( rows.size (), config::dialog::RULE_THICKNESS );

		// Its padding: the text rows end where the button box begins (the grid leaves no gap, measured), so the
		// padding above is the rule's distance into the box; below it, the distance to the first button.

		const int ruleTop    = rows.first ();
		const int buttonsTop = box.button ( QMessageBox::Yes )->mapTo ( &box, QPoint ( 0, 0 ) ).y ();

		QCOMPARE ( ruleTop - buttons->y (),                                   config::dialog::RULE_PADDING_ABOVE );
		QCOMPARE ( buttonsTop - ( ruleTop + config::dialog::RULE_THICKNESS ), config::dialog::RULE_PADDING_BELOW );

		// And it survives a style change, which rebuilds the grid under an open box (measured).

		install_application_style ();

		QCoreApplication::processEvents ();

		QCOMPARE ( rule_rows ( box ).size (), config::dialog::RULE_THICKNESS );
	}

	void content_and_buttons_sit_at_the_framed_dialogs_inset ()
	{
		MessageBox box ( MessageKind::Error, QStringLiteral ( "Print" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		const int margin = config::dialog::CONTENT_MARGIN;

		const QDialogButtonBox* const buttons = box.findChild<QDialogButtonBox*> ();

		QVERIFY ( buttons != nullptr );
		QVERIFY ( icon_label ( box ) != nullptr );

		QCOMPARE ( icon_label ( box )->x (), margin );
		QCOMPARE ( icon_label ( box )->y (), margin );
		QCOMPARE ( buttons->geometry ().right  () + 1, box.width  () - margin );
		QCOMPARE ( buttons->geometry ().bottom () + 1, box.height () - margin );
	}

	void message_stays_at_the_top_and_the_space_opens_above_the_buttons ()
	{
		// Unstretched, the floor's extra height was split between the message row and the one beneath it, and the
		// message floated down the box (measured). The message label keeps its natural height at the top inset.

		MessageBox box ( MessageKind::Error, QStringLiteral ( "Print" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		const QLabel* const label = message_label ( box );

		QVERIFY ( label != nullptr );

		QCOMPARE ( label->y (),      config::dialog::CONTENT_MARGIN );
		QCOMPARE ( label->height (), label->sizeHint ().height () );
		QVERIFY  ( label->alignment ().testFlag ( Qt::AlignTop ) );
	}

	void informative_text_follows_the_message_directly ()
	{
		MessageBox box ( MessageKind::Warning, QStringLiteral ( "VJE" ), QStringLiteral ( "Save changes?" ), QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		box.setInformativeText ( QStringLiteral ( "Your changes will be lost if you don't save them." ) );

		show_and_settle ( box );

		const QLabel* const label       = message_label ( box );
		const QLabel* const informative = box.findChild<QLabel*> ( QStringLiteral ( "qt_msgbox_informativelabel" ) );

		QVERIFY ( ( label != nullptr ) && ( informative != nullptr ) );

		// The informative label may take the extra height, but its text is top-aligned, so it reads directly under the
		// message: nothing between the two but the grid's own row spacing.

		QCOMPARE ( label->height (), label->sizeHint ().height () );
		QVERIFY  ( informative->alignment ().testFlag ( Qt::AlignTop ) );
		QVERIFY  ( informative->y () - ( label->y () + label->height () ) <= box.layout ()->spacing () + 1 );
	}

	//=================================================================================================================
	// (3) Captions and order
	//=================================================================================================================

	void standard_buttons_carry_vjes_captions ()
	{
		MessageBox save ( MessageKind::Warning, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save, QMessageBox::Cancel );

		QCOMPARE ( save.button ( QMessageBox::Save    )->text (), QStringLiteral ( "&Save" ) );
		QCOMPARE ( save.button ( QMessageBox::Discard )->text (), QStringLiteral ( "Do&n't Save" ) );
		QCOMPARE ( save.button ( QMessageBox::Cancel  )->text (), QStringLiteral ( "Cancel" ) );

		MessageBox question ( MessageKind::Warning, QStringLiteral ( "Paste" ), SHORT_TEXT, QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::No );

		QCOMPARE ( question.button ( QMessageBox::Yes )->text (), QStringLiteral ( "&Yes" ) );
		QCOMPARE ( question.button ( QMessageBox::No  )->text (), QStringLiteral ( "&No" ) );
	}

	void buttons_are_in_windows_order_whatever_the_base_style_says ()
	{
		// Guard: the stand-in base style really does reorder the buttons, so the second half is a real override.

		QApplication::setStyle ( new GnomeLikeStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ) );

		{
			MessageBox box ( MessageKind::Warning, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save, QMessageBox::Cancel );

			show_and_settle ( box );

			QVERIFY2
			(
				captions_left_to_right ( box ).first () != QStringLiteral ( "&Save" ),
				"the GNOME-like base style should not put Save first"
			);
		}

		QApplication::setStyle ( new FluentStyle ( new GnomeLikeStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ) ) );

		MessageBox box ( MessageKind::Warning, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save, QMessageBox::Cancel );

		show_and_settle ( box );

		QCOMPARE ( captions_left_to_right ( box ), ( QStringList { QStringLiteral ( "&Save" ), QStringLiteral ( "Do&n't Save" ), QStringLiteral ( "Cancel" ) } ) );

		// Right-aligned rather than centred: the last button ends where the button box ends.

		const QDialogButtonBox* const buttonBox = box.findChild<QDialogButtonBox*> ();

		QVERIFY ( buttonBox != nullptr );
		QCOMPARE ( box.button ( QMessageBox::Cancel )->geometry ().right (), buttonBox->rect ().right () );
	}

	void windows_order_holds_under_both_interface_styles ()
	{
		// LD-9 is a convention, not a look: Classic, which turns FluentStyle's shape overrides off, keeps it.

		const config::appearance::InterfaceStyle styles [] =
		{
			config::appearance::InterfaceStyle::Fluent,
			config::appearance::InterfaceStyle::Classic
		};

		for ( const config::appearance::InterfaceStyle interfaceStyle : styles )
		{
			FluentStyle style ( new GnomeLikeStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ), interfaceStyle );

			QCOMPARE ( style.styleHint ( QStyle::SH_DialogButtonLayout ),       int ( QDialogButtonBox::WinLayout ) );
			QCOMPARE ( style.styleHint ( QStyle::SH_MessageBox_CenterButtons ), 0 );
		}
	}

	//=================================================================================================================
	// (2) Text alignment
	//=================================================================================================================

	void centred_text_is_painted_in_the_middle_both_ways ()
	{
		// Guard: by default the text is painted well away from the middle, so the centred case below measures the
		// alignment and not a message that happened to land there.

		MessageBox plain ( MessageKind::Information, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( plain );

		const QRect plainArea = text_area ( plain );
		const QRect plainInk  = ink_rect  ( plain, plainArea );

		QVERIFY ( !plainInk.isEmpty () );
		QVERIFY ( std::abs ( plainInk.center ().x () - plainArea.center ().x () ) > 20 );
		QVERIFY ( std::abs ( plainInk.center ().y () - plainArea.center ().y () ) > 20 );

		MessageBox box
		(
			MessageKind::Information, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok,
			nullptr, HorizontalTextAlignment::Centre, VerticalTextAlignment::Centre
		);

		show_and_settle ( box );

		const QRect area = text_area ( box );
		const QRect ink  = ink_rect  ( box, area );

		// A tolerance for the font, not for the alignment: ink is not the line box, and a line's descent is not its
		// ascent, so the ink of a centred line sits a pixel or two off the arithmetic middle.

		QVERIFY2 ( std::abs ( ink.center ().x () - area.center ().x () ) <= 2, qPrintable ( QStringLiteral ( "ink %1, area %2" ).arg ( ink.center ().x () ).arg ( area.center ().x () ) ) );
		QVERIFY2 ( std::abs ( ink.center ().y () - area.center ().y () ) <= 4, qPrintable ( QStringLiteral ( "ink %1, area %2" ).arg ( ink.center ().y () ).arg ( area.center ().y () ) ) );
	}

	void bottom_right_text_is_painted_against_the_buttons_and_the_edge ()
	{
		MessageBox box
		(
			MessageKind::Information, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok,
			nullptr, HorizontalTextAlignment::Right, VerticalTextAlignment::Bottom
		);

		show_and_settle ( box );

		const QRect area = text_area ( box );
		const QRect ink  = ink_rect  ( box, area );

		QVERIFY2 ( area.right  () - ink.right  () <= 3, qPrintable ( QStringLiteral ( "ink %1, area %2" ).arg ( ink.right  () ).arg ( area.right  () ) ) );
		QVERIFY2 ( area.bottom () - ink.bottom () <= 6, qPrintable ( QStringLiteral ( "ink %1, area %2" ).arg ( ink.bottom () ).arg ( area.bottom () ) ) );
	}

	void a_message_and_its_informative_text_are_centred_as_one_block ()
	{
		MessageBox box
		(
			MessageKind::Warning, QStringLiteral ( "VJE" ), QStringLiteral ( "Save changes?" ), QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok,
			nullptr, HorizontalTextAlignment::Left, VerticalTextAlignment::Centre
		);

		box.setInformativeText ( QStringLiteral ( "Your changes will be lost." ) );

		show_and_settle ( box );

		const QRect area = text_area ( box );
		const QRect ink  = ink_rect  ( box, area );

		// One block: the two sentences sit together (no more than three lines' height of ink from the top of the first to
		// the bottom of the second), and the block, not each sentence, is in the middle.
		//
		// In the middle TO WITHIN QT'S OWN MARGIN. Qt gives the informative label 7 px of contents margin above and below
		// (measured) and lays the grid out without counting it, so the block's ink sits about 5 px below the middle
		// (measured: 69 against 64). Compensating exactly would mean re-measuring after every layout; the one centred box
		// in the application, the version box, has no informative text. So the bound is that margin, read from the label
		// rather than guessed, plus the font's own 4.

		const QLabel* const informative = box.findChild<QLabel*> ( QStringLiteral ( "qt_msgbox_informativelabel" ) );

		QVERIFY ( informative != nullptr );

		const int tolerance = 4 + informative->contentsMargins ().top ();

		QVERIFY2 ( ink.height () <= 3 * QFontMetrics ( box.font () ).height (), qPrintable ( QStringLiteral ( "ink height %1" ).arg ( ink.height () ) ) );
		QVERIFY2 ( std::abs ( ink.center ().y () - area.center ().y () ) <= tolerance, qPrintable ( QStringLiteral ( "ink %1, area %2" ).arg ( ink.center ().y () ).arg ( area.center ().y () ) ) );
	}

	void the_icon_stays_top_left_whatever_the_text_does ()
	{
		// Every alignment the text can take, the icon at the inset's top-left corner in each. The centred and
		// bottom-right boxes are the ones that fail against an icon that travels with the text.

		const QList<QPair<HorizontalTextAlignment, VerticalTextAlignment>> alignments =
		{
			{ HorizontalTextAlignment::Left,   VerticalTextAlignment::Top    },
			{ HorizontalTextAlignment::Centre, VerticalTextAlignment::Centre },
			{ HorizontalTextAlignment::Right,  VerticalTextAlignment::Bottom }
		};

		for ( const auto& [ horizontal, vertical ] : alignments )
		{
			MessageBox box
			(
				MessageKind::Information, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok,
				nullptr, horizontal, vertical
			);

			show_and_settle ( box );

			QCOMPARE ( icon_label ( box )->pos (), QPoint ( config::dialog::CONTENT_MARGIN, config::dialog::CONTENT_MARGIN ) );
		}
	}

	void alignment_can_change_on_an_open_box ()
	{
		MessageBox box ( MessageKind::Information, QStringLiteral ( "VJE" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		box.set_text_alignment ( HorizontalTextAlignment::Centre, VerticalTextAlignment::Centre );

		QCoreApplication::processEvents ();

		const QRect area = text_area ( box );
		const QRect ink  = ink_rect  ( box, area );

		QVERIFY ( std::abs ( ink.center ().x () - area.center ().x () ) <= 2 );
		QVERIFY ( std::abs ( ink.center ().y () - area.center ().y () ) <= 4 );
		QCOMPARE ( box.size (), floor_size () );   // Moving the text does not resize the box.
	}

	//=================================================================================================================
	// (4) Default and escape; (5) the icon
	//=================================================================================================================

	void default_and_escape_are_the_ones_named ()
	{
		// Deliberately the OPPOSITE of what Qt would guess for OK | Cancel (default OK, escape Cancel), so the case
		// fails against a box that leaves either to Qt.

		MessageBox box ( MessageKind::Warning, QStringLiteral ( "T" ), SHORT_TEXT, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel, QMessageBox::Ok );

		QCOMPARE ( static_cast<QAbstractButton*> ( box.defaultButton () ), box.button ( QMessageBox::Cancel ) );
		QCOMPARE ( box.escapeButton (),                                      box.button ( QMessageBox::Ok ) );
	}

	void icon_follows_the_kind ()
	{
		QCOMPARE ( message_box_icon ( MessageKind::Information ), QMessageBox::Information );
		QCOMPARE ( message_box_icon ( MessageKind::Error       ), QMessageBox::Critical );
		QCOMPARE ( message_box_icon ( MessageKind::Warning     ), QMessageBox::Warning );

		MessageBox box ( MessageKind::Error, QStringLiteral ( "T" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		QCOMPARE ( box.icon (), QMessageBox::Critical );
	}

	void the_icon_is_drawn_at_the_configured_size ()
	{
		// Guard: the base style's own size is a different number, so the case measures FluentStyle's answer and not a
		// coincidence.

		const std::unique_ptr<QStyle> fusion ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) );

		QVERIFY ( fusion->pixelMetric ( QStyle::PM_MessageBoxIconSize ) != config::message_box::ICON_SIZE );

		MessageBox box ( MessageKind::Information, QStringLiteral ( "T" ), SHORT_TEXT, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		show_and_settle ( box );

		// The pixmap, in logical pixels, and the label that holds it: the icon is drawn at that size, not scaled into it.

		const QPixmap pixmap = icon_label ( box )->pixmap ();

		QCOMPARE ( pixmap.deviceIndependentSize ().toSize (), QSize ( config::message_box::ICON_SIZE, config::message_box::ICON_SIZE ) );
		QCOMPARE ( icon_label ( box )->size (),                QSize ( config::message_box::ICON_SIZE, config::message_box::ICON_SIZE ) );
	}

	//=================================================================================================================
	// The answer
	//=================================================================================================================

	void ask_returns_the_button_chosen ()
	{
		QTimer::singleShot ( 0, this, [] ()
		{
			if ( QMessageBox* const box = qobject_cast<QMessageBox*> ( QApplication::activeModalWidget () ) )
			{
				box->button ( QMessageBox::Yes )->click ();
			}
		} );

		const QMessageBox::StandardButton answer = ask_message_box
		(
			nullptr,
			MessageKind::Warning,
			QStringLiteral ( "Paste" ),
			SHORT_TEXT,
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No,
			QMessageBox::No
		);

		QCOMPARE ( answer, QMessageBox::Yes );
	}
};

QTEST_MAIN ( TestMessageBox )

#include "tst_message_box.moc"

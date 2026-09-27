//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GroupBox -- SET-01c's group box: a one-pixel outline with its title set into the top edge, the classic shape, drawn
//   from the palette. The Settings dialog divides every page into these (dialogs/SettingsDialog).
//
//   A QGroupBox SUBCLASS, the shape dialogs/MessageBox has, rather than a widget of its own. What Qt's group box already
//   is, it keeps: assistive technology reads it as a GROUP named by its title (NFR-05), it is not a Tab stop, and it
//   takes the layout's group-box spacing. What Qt draws, it replaces, for three reasons read from Qt's source (6.8.3 and
//   6.10.1, identical on each):
//
//     1. THE FRAME. Fusion draws a group box's frame from a fixed image, fusion_groupbox.png -- a one-pixel #757575 line
//        at 20 % opacity, with ~2 px corners. Composited on VJE's window colours that is 25 lightness levels below the
//        light surface and 14 above the dark one: uneven between the themes, fainter than the dialog's own rule, and
//        the same corners under both interface styles. This box strokes its outline in style/dialog_surface's frame
//        colour instead -- a distance from the surface, so it is the same step on both themes -- and rounds its corners
//        under both interface styles (SET-01c).
//
//     2. THE PADDING. QGroupBox re-derives its contents margins from the STYLE whenever its title, font or style
//        changes (QGroupBoxPrivate::calculateFrame, from changeEvent) -- and ThemeService installs a new style on every
//        theme change, including an OS dark-mode switch under Theme = System while a dialog is open. So the box states
//        its padding in config::group_box and re-applies it after Qt has had its turn, every time.
//
//     3. THE TITLE. QGroupBox::setTitle parses the title for a mnemonic, and an ampersand followed by ANY printable
//        character counts -- a space included (QKeySequence::mnemonic). "Theme & Style" would claim Alt+Space, the
//        window's system menu, and draw the ampersand as an underlined space. So a title here is PLAIN TEXT: it is
//        escaped before Qt sees it, and drawn as given.
//
//   NOT FluentStyle's, deliberately: that class changes shape and metrics and never colour, and an outline has to be
//   stroked in one. Nor a stylesheet, which the application does not use and which would put QStyleSheetStyle into the
//   paint path of every control inside the box.
//
//   It knows nothing about settings, so any dialog can use it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "AppConfig.hpp"

#include <QGroupBox>
#include <QString>

class QEvent;
class QPaintEvent;
class QRect;
class QWidget;

namespace vje
{
	//*****************************************************************************************************************
	// Class: GroupBox
	//*****************************************************************************************************************

	class GroupBox : public QGroupBox
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// title is PLAIN TEXT, shown exactly as given: an ampersand in it is an ampersand, never a mnemonic.

		explicit GroupBox ( const QString& title, QWidget* parent = nullptr );

		//=============================================================================================================
		// Accessors
		//=============================================================================================================

	public:

		// The title as it was given -- not QGroupBox::title (), which holds it escaped for Qt's mnemonic parser.

		QString plain_title () const;

		bool corners_rounded () const;

		// Where the outline's top edge runs: the title's vertical centre, in the box's own coordinates. Zero for a box
		// with no title, whose outline then runs along its top like the other three sides.

		int frame_top () const;

		// Where the title is drawn, in the box's own coordinates. Empty for a box with no title.

		QRect title_rect () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Rounded (the default) or square. PUSHED IN rather than read from a settings store or from the style, for Card's
		// reason -- a general container knows nothing about the application's settings.

		void set_corners_rounded ( bool rounded );

		//=============================================================================================================
		// Event Handlers
		//=============================================================================================================

	protected:

		void paintEvent  ( QPaintEvent* event ) override;
		void changeEvent ( QEvent*      event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// The padding of config::group_box, over whatever QGroupBox has just derived from the style.

		void apply_padding ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QString plainTitle;

		// Rounded by default under BOTH interface styles (SET-01c, revised 2026-09-27): a box's fillet is part of the group
		// box's own shape, not SET-12's choice. set_corners_rounded remains for a caller that wants a square box.

		bool cornersRounded = true;
	};
}

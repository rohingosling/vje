//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   AboutDialog -- Help > About VJE (HELP-04): the product name and its expansion, the version, the copyright, and a
//   link to the project page.
//
//   WHY IT IS A DIALOG AND NOT A QMessageBox. It was QMessageBox::about until Phase 15a, which is the one place that
//   call was left in the application -- and it is the one prompt whose content is NOT "an icon, a sentence and
//   buttons". About has a content area, so it takes STYLE-15's structure like every other dialog VJE lays out itself;
//   the FILE-08 dirty gate, the error boxes and the confirmations stay QMessageBox for exactly the reason this does
//   not.
//
//   THE LINK IS A HAND-OFF, NOT A REQUEST. QLabel::setOpenExternalLinks hands the URL to the desktop's browser; the
//   application opens no connection of its own, so UPD-03's "nothing is transmitted but the update feed request"
//   still holds.
//
//   TWO ICONS, AND THEY ARE DIFFERENT THINGS (2026-08-13). `icon` is the dialog's own title-bar glyph, taken from the
//   shared icon set like every other dialog's under STYLE-15. `applicationIcon` is VJE's own application icon, shown
//   at size beside the text -- the product's mark, in the one dialog whose job is to present the product. Both are
//   parameters rather than calls the dialog makes for itself, which is dialog_frame's rule and its reason: an icon
//   fetched inside is the part that gets forgotten, and passing them keeps this class knowing nothing about
//   IconLibrary or about where the application icon is compiled in.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QDialog>

class QIcon;
class QLabel;

namespace vje
{
	//*****************************************************************************************************************
	// Class: AboutDialog
	//*****************************************************************************************************************

	class AboutDialog : public QDialog
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		AboutDialog ( const QIcon& icon, const QIcon& applicationIcon, QWidget* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// For tests and for anything that needs to read what the dialog is claiming.

		QLabel* content_label () const;

		// The label carrying the application icon. Exposed for the same reason: what it holds is a rendered pixmap,
		// and a pixmap claim is only checked by reading the pixmap (lessons-learned Q12).

		QLabel* icon_label () const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QLabel* contentLabel = nullptr;
		QLabel* iconLabel    = nullptr;
	};
}

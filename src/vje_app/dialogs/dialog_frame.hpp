//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   dialog_frame -- STYLE-15's four parts, assembled onto a dialog: a title bar carrying the dialog's own icon, a
//   content area, a horizontal rule closing it, and a right-aligned button row beneath.
//
//   A FREE FUNCTION OVER A QDialog, NOT A BASE CLASS. SettingsDialog, GoToDialog and XmlImportDialog already derive
//   from QDialog and each owns its own layout, so a StandardDialog base would change three hierarchies to share four
//   widgets. This is printing/page_furniture's shape, for the same reason: what it draws can be checked without the
//   machine that normally drives it.
//
//   WHAT IT ABSORBS, AND WHY THAT IS THE POINT. Three things the existing dialogs each decided separately are now
//   decided once -- the modality, the removal of the title bar's context-help button (which GoToDialog and
//   XmlImportDialog both stripped and SettingsDialog did not), and the inset the content sits at. Stating the
//   structure once is what stops the next dialog approximating it differently, and the icon is a PARAMETER rather
//   than a call the caller makes afterwards for exactly that reason: it is the part that would otherwise be forgotten.
//
//   THE RULE ALSO REACHES DIALOGS WHOSE LAYOUT IS QT'S (apply_button_rule, 2026-09-26): the message boxes and the text
//   prompts. Their layouts cannot take the frame, so the rule is added to them instead -- the same line, with the same
//   padding above and below it, over the same button row. So every modal VJE shows closes its content the same way.
//
//   WHAT IT DELIBERATELY DOES NOT COVER. The platform pickers (Open, Save As, a folder, Page Setup, Print) are the
//   toolkit's, which is the whole reason for using them.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

class QDialog;
class QDialogButtonBox;
class QIcon;
class QWidget;

namespace vje
{
	// Installs the frame on `dialog`, which must not already own a layout.
	//
	// `content` is reparented and takes the vertical stretch -- it should carry NO margins of its own, since the inset
	// is the frame's to state. `buttons` spans the row's full width rather than being pushed right by a stretch,
	// because QDialogButtonBox places its own buttons by ROLE: accept and reject land at the right on every platform,
	// while a ResetRole button (SettingsDialog's Restore Defaults) lands at the opposite end deliberately, and a
	// stretch in front of the box would collapse that separation into a single group.

	void apply_dialog_frame ( QDialog& dialog, QWidget* content, QDialogButtonBox* buttons, const QIcon& icon );

	// The frame's rule and its padding, for a dialog whose layout is Qt's rather than ours -- a QMessageBox, a
	// QInputDialog -- so it closes its content exactly as a framed dialog does. The rule runs edge to edge across the
	// dialog, config::dialog::RULE_PADDING_ABOVE below the content and RULE_PADDING_BELOW above the buttons.
	//
	// gapAboveButtons is the space the dialog's own layout already leaves between its content and the button box, which
	// counts toward the padding above the rule (a QInputDialog's layout spacing; a QMessageBox's grid leaves none,
	// measured). Once per dialog: a second call does nothing.

	void apply_button_rule ( QDialog& dialog, QDialogButtonBox& buttons, int gapAboveButtons );
}

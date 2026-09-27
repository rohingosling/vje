//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   MessageBox -- STYLE-18's one structure for every message box: an icon naming the KIND of message, a size floor the
//   box grows past for a longer message, STYLE-15's inset, the message at the top beside the icon, and VJE's own
//   captions on the standard buttons. The button ORDER is not decided here: FluentStyle answers it for every button
//   box in the application (LD-9).
//
//   A SUBCLASS, NOT A FREE FUNCTION -- the opposite of dialogs/dialog_frame, for a measured reason. dialog_frame is a
//   free function because its dialogs already derive from QDialog; nothing derives from QMessageBox, so a subclass
//   costs no hierarchy. And it is REQUIRED: QMessageBox rebuilds its own grid on setInformativeText and on a style
//   change, and each rebuild was measured dropping a floor applied beforehand (Qt 6.10.1, 2026-09-26). So the floor is
//   applied when the box is SHOWN, and again whenever the grid it was applied to has been replaced -- which only the
//   box itself can see.
//
//   show_message_box and ask_message_box are the two shapes every call site uses. A box needing more (FILE-08's
//   informative text, the command line's preformatted text) constructs a MessageBox directly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QMessageBox>
#include <QPointer>
#include <QString>

class QEvent;
class QLayout;
class QShowEvent;
class QWidget;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The kind of message a box carries (STYLE-18 (5)). The icon follows from the kind rather than being chosen at the
	// call site, which is what stops the same kind of message reaching the user under two different icons.
	//
	// There is no Question kind. Windows guidance retires the question-mark icon: a box asking something is identified
	// by its buttons, and the icon states what is at stake.
	//-----------------------------------------------------------------------------------------------------------------

	enum class MessageKind
	{
		Information,   // A notice: nothing failed and nothing is asked (SET-09's log-folder fallback).
		Error,         // An operation that failed (FILE-06, a failed save, import, export or print).
		Warning        // A refusal (VAL-05, FILE-11's refused export), or a question whose Yes changes or discards something.
	};

	// The icon a kind of message carries.

	QMessageBox::Icon message_box_icon ( MessageKind kind );

	// VJE's caption for a standard button, identical on every platform (STYLE-18 (3)), or an empty string for a button
	// VJE does not caption -- which then keeps the platform theme's. Discard reads "Don't Save", FILE-08's word and
	// Windows', rather than the caption Qt gives it on Windows ("Discard", measured); a box whose Discard means
	// something other than not saving re-captions it after construction.

	QString standard_button_caption ( QMessageBox::StandardButton button );

	//-----------------------------------------------------------------------------------------------------------------
	// Where a box's text sits (STYLE-18 (2)): horizontally within the text column beside the icon, and vertically within
	// the area between the top inset and the buttons, which the size floor makes taller than a short message. Top-left
	// is the default and what every box but the command line's version box uses. The text moves as ONE BLOCK -- a
	// message and its informative text stay together. The icon does NOT move: it stays top-left whatever the text does.
	//-----------------------------------------------------------------------------------------------------------------

	enum class HorizontalTextAlignment
	{
		Left,
		Centre,
		Right
	};

	enum class VerticalTextAlignment
	{
		Top,
		Centre,
		Bottom
	};

	//*****************************************************************************************************************
	// Class: MessageBox
	//*****************************************************************************************************************

	class MessageBox : public QMessageBox
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The default and escape buttons are REQUIRED (STYLE-18 (4)): Esc never loses work, and on a destructive
		// question Enter picks the answer that loses nothing -- a rule each caller states rather than inherits from
		// whichever button Qt happens to guess.

		MessageBox
		(
			MessageKind                  kind,
			const QString&               title,
			const QString&               text,
			QMessageBox::StandardButtons buttons,
			QMessageBox::StandardButton  defaultButton,
			QMessageBox::StandardButton  escapeButton,
			QWidget*                     parent     = nullptr,
			HorizontalTextAlignment      horizontal = HorizontalTextAlignment::Left,
			VerticalTextAlignment        vertical   = VerticalTextAlignment::Top
		);

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Moves the text, before the box is shown or while it is open.

		void set_text_alignment ( HorizontalTextAlignment horizontal, VerticalTextAlignment vertical );

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Runs the box modally and returns the button chosen, read exactly as ask_message_box reads it -- for a caller
		// that built the box itself (to add informative text, or re-caption a button) and still wants the one reading.

		QMessageBox::StandardButton ask ();

		//=============================================================================================================
		// Event Handlers
		//=============================================================================================================

	protected:

		void showEvent   ( QShowEvent* event ) override;
		void changeEvent ( QEvent*     event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// The floor, the inset and the text placement, applied to whatever grid the box has now. Idempotent per grid.

		void apply_structure ();

		// The text alignment alone, on the current grid: which rows take the floor's extra height, and how the labels
		// and the icon sit in them. Safe to repeat, so the setter can call it on an open box.

		void apply_alignment ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		// The grid apply_structure last structured. QMessageBox DELETES its grid when it rebuilds one, so this reads
		// null afterwards -- which is how the box tells that its structure has gone.

		QPointer<QLayout> structuredLayout;

		HorizontalTextAlignment horizontalAlignment = HorizontalTextAlignment::Left;
		VerticalTextAlignment   verticalAlignment   = VerticalTextAlignment::Top;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A notice with one OK button, which is both its default and its escape.
	//-----------------------------------------------------------------------------------------------------------------

	void show_message_box
	(
		QWidget*                parent,
		MessageKind             kind,
		const QString&          title,
		const QString&          text,
		HorizontalTextAlignment horizontal = HorizontalTextAlignment::Left,
		VerticalTextAlignment   vertical   = VerticalTextAlignment::Top
	);

	//-----------------------------------------------------------------------------------------------------------------
	// A question. Returns the button the user chose; Cancel when the box was dismissed without one, and NoButton when it
	// was accepted without one -- exactly what Qt's own static QMessageBox functions return, so no caller's reading of
	// the answer changes by moving onto this.
	//-----------------------------------------------------------------------------------------------------------------

	QMessageBox::StandardButton ask_message_box
	(
		QWidget*                     parent,
		MessageKind                  kind,
		const QString&               title,
		const QString&               text,
		QMessageBox::StandardButtons buttons,
		QMessageBox::StandardButton  defaultButton,
		QMessageBox::StandardButton  escapeButton,
		HorizontalTextAlignment      horizontal = HorizontalTextAlignment::Left,
		VerticalTextAlignment        vertical   = VerticalTextAlignment::Top
	);
}

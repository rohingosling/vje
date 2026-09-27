//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TitleBarSync -- STYLE-19's colour applied to every title bar VJE shows: the main window, the framed dialogs and the
//   message boxes, from one place and with no call site involved.
//
//   AN EVENT FILTER ON THE APPLICATION. Every top-level window with a title bar is coloured when it is SHOWN, which is
//   before its first frame is on screen, so it never appears in the platform's colour first. A popup menu or a tooltip
//   has no title bar and is left alone. refresh () re-colours every visible window; the composition root connects it
//   to ThemeService::applied, since the colour is cut from the palette.
//
//   THE COLOUR ONLY. Whether a title bar is dark or light is Qt's: it derives the flag from the window's palette, which
//   is VJE's theme, and re-derives it on every palette change -- so setting it here as well was measured losing to Qt.
//
//   THE PLATFORM CALL AND THE USER'S PRECEDENCE ARE INJECTED, which is what lets tst_title_bar drive the filter
//   offscreen and record what it was asked for.
//
//   ROOT-SCOPED, constructed in main.cpp: an event filter on the whole application is not something a window owns. The
//   command line's message box (CLI-07) installs one of its own.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>
#include <QObject>

#include <functional>
#include <optional>

class QEvent;
class QWidget;

namespace vje
{
	//*****************************************************************************************************************
	// Class: TitleBarSync
	//*****************************************************************************************************************

	class TitleBarSync : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Types
		//=============================================================================================================

	public:

		using OwnershipQuery = std::function<bool ()>;
		using Applier        = std::function<void ( QWidget& window, const std::optional<QColor>& caption )>;

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Installs itself on the application, which must already exist.

		explicit TitleBarSync
		(
			Applier        applier        = platform_applier (),
			OwnershipQuery userOwnsColour = platform_ownership (),
			QObject*       parent         = nullptr
		);

		~TitleBarSync () override;

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		// Re-colours every visible top-level window with a title bar.

		void refresh ();

		// The platform's call and the platform's answer, as the defaults.

		static Applier        platform_applier   ();
		static OwnershipQuery platform_ownership ();

		// Whether a widget is a window with a title bar: a top-level Window or Dialog, not frameless. Popups, tooltips
		// and the like are windows with none.

		static bool has_title_bar ( const QWidget& widget );

		//=============================================================================================================
		// Event Handlers
		//=============================================================================================================

	protected:

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void apply_to ( QWidget& window );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		Applier        applier;
		OwnershipQuery userOwnsColour;
	};
}

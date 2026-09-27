//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TitleBarSync implementation. See the header for why it is an application event filter, and why it sets the colour
//   and leaves dark-or-light to Qt.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/TitleBarSync.hpp"

#include "style/title_bar_surface.hpp"

#include <platform/title_bar.hpp>

#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TitleBarSync::TitleBarSync ( Applier applier, OwnershipQuery userOwnsColour, QObject* parent )
	:	QObject        ( parent )
	,	applier        ( std::move ( applier ) )
	,	userOwnsColour ( std::move ( userOwnsColour ) )
	{
		QCoreApplication::instance ()->installEventFilter ( this );
	}

	TitleBarSync::~TitleBarSync ()
	{
		if ( QCoreApplication* const application = QCoreApplication::instance () )
		{
			application->removeEventFilter ( this );
		}
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	void TitleBarSync::refresh ()
	{
		for ( QWidget* const widget : QApplication::topLevelWidgets () )
		{
			if ( widget->isVisible () && has_title_bar ( *widget ) )
			{
				apply_to ( *widget );
			}
		}
	}

	TitleBarSync::Applier TitleBarSync::platform_applier ()
	{
		return [] ( QWidget& window, const std::optional<QColor>& caption )
		{
			const std::optional<std::uint32_t> captionRgb = caption.has_value ()
			                                              ? std::optional<std::uint32_t> ( caption->rgb () & 0xFFFFFFu )
			                                              : std::nullopt;

			platform::apply_title_bar_colour ( static_cast<quintptr> ( window.winId () ), captionRgb );
		};
	}

	TitleBarSync::OwnershipQuery TitleBarSync::platform_ownership ()
	{
		return [] () { return platform::user_owns_title_bar_colour (); };
	}

	bool TitleBarSync::has_title_bar ( const QWidget& widget )
	{
		if ( !widget.isWindow () || widget.windowFlags ().testFlag ( Qt::FramelessWindowHint ) )
		{
			return false;
		}

		const Qt::WindowType type = widget.windowType ();

		return ( type == Qt::Window ) || ( type == Qt::Dialog );
	}

	//=================================================================================================================
	// Event Handlers
	//=================================================================================================================

	bool TitleBarSync::eventFilter ( QObject* watched, QEvent* event )
	{
		// The type test comes first: this filter sees every event in the application, and all but a handful of them
		// end here.

		if ( event->type () == QEvent::Show )
		{
			if ( QWidget* const widget = qobject_cast<QWidget*> ( watched ); ( widget != nullptr ) && has_title_bar ( *widget ) )
			{
				apply_to ( *widget );
			}
		}

		return QObject::eventFilter ( watched, event );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void TitleBarSync::apply_to ( QWidget& window )
	{
		// The APPLICATION's palette, not the window's. refresh () runs on ThemeService::applied, straight after the new
		// palette is set, and QApplication::setPalette has not reached existing widgets by then (lesson Q9): read from
		// the window, a switch to Dark coloured the title bar from the old light surface (tst_title_bar, measured).

		applier ( window, title_bar_caption ( QApplication::palette (), userOwnsColour () ) );
	}
}

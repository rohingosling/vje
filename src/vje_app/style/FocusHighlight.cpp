//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FocusHighlight implementation. See the header for why widget focus needs answering at all when Qt already has an
//   Inactive palette group, and for why the decision itself is a free function.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FocusHighlight.hpp"

#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace vje
{
	//=================================================================================================================
	// The rule
	//=================================================================================================================

	QList<QPalette::ColorRole> palette_roles_for ( FocusRoles roles )
	{
		switch ( roles )
		{
			case FocusRoles::Selection: return { QPalette::Highlight, QPalette::HighlightedText };
			case FocusRoles::Surface:   return { QPalette::Button };
		}

		return {};
	}

	QPalette focus_adjusted_palette
	(
		const QPalette& current,
		const QPalette& theme,
		bool            holdsFocus,
		FocusRoles      roles
	)
	{
		// Read from the group that answers the question, then write into the group the widget actually paints from.
		// Reading `theme` rather than `current` is what makes this idempotent: a palette this function has already
		// muted would otherwise be its own source, and the focused colour could never come back.

		const QPalette::ColorGroup group = holdsFocus ? QPalette::Active : QPalette::Inactive;

		QPalette adjusted = current;

		for ( const QPalette::ColorRole role : palette_roles_for ( roles ) )
		{
			adjusted.setColor ( QPalette::Active, role, theme.color ( group, role ) );
		}

		return adjusted;
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FocusHighlight* FocusHighlight::install ( QWidget* widget, FocusRoles roles, QWidget* focusScope )
	{
		return ( widget != nullptr ) ? new FocusHighlight ( widget, roles, focusScope ) : nullptr;
	}

	FocusHighlight::FocusHighlight ( QWidget* widget, FocusRoles roles, QWidget* focusScope )
		: QObject ( widget )
		, target  ( widget )
		, scope   ( ( focusScope != nullptr ) ? focusScope : widget )
		, governedRoles ( roles )
	{
		widget->installEventFilter ( this );

		// QApplication::focusChanged rather than the widget's own FocusIn / FocusOut, because it names BOTH sides of
		// the move. A FocusOut arrives while the answer to "where did it go?" is still in flux, and that answer is
		// exactly what the cell-editor case in holds_focus() turns on.

		connect ( qApp, &QApplication::focusChanged, this, &FocusHighlight::handle_focus_changed );

		refresh ();
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void FocusHighlight::refresh ()
	{
		refreshPending = false;

		const bool focused = holds_focus ();

		target->setPalette
		(
			focus_adjusted_palette ( target->palette (), QApplication::palette (), focused, governedRoles )
		);

		// On the TRANSITION only. A theme change re-runs this whole decision, and a widget repainting its chrome because
		// of it would be answering "the keyboard moved" to a question nobody asked.

		if ( focused != lastFocusState )
		{
			lastFocusState = focused;

			emit focus_state_changed ( focused );
		}
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	bool FocusHighlight::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( watched == target )
		{
			// Writing a role onto a widget makes Qt treat it as the widget's own and stop propagating it, so a theme
			// switch does NOT reach the two roles written here -- without this, the previous theme's bar outlives it.
			//
			// StyleChange is the reliable member of this set, and the reason all three are listened for. ThemeService
			// applies a theme as setStyle() followed by setPalette(), and only the style change is delivered to a
			// widget; a bare setPalette() propagates to nothing already constructed. Measured, not assumed -- see the
			// note on tst_focus_highlight.

			const QEvent::Type type = event->type ();

			const bool isThemeEvent = ( type == QEvent::StyleChange )              ||
			                          ( type == QEvent::ApplicationPaletteChange ) ||
			                          ( type == QEvent::PaletteChange );

			if ( isThemeEvent )
			{
				schedule_refresh ();
			}
		}

		return QObject::eventFilter ( watched, event );
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void FocusHighlight::handle_focus_changed ( QWidget* lost, QWidget* gained )
	{
		// Only a move that crosses this widget's boundary changes its answer. Focus travelling between two widgets
		// elsewhere in the application is none of its business, and repainting for it would put every item view in the
		// window on the path of every focus change.

		if ( belongs_to_us ( lost ) || belongs_to_us ( gained ) )
		{
			refresh ();
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool FocusHighlight::belongs_to_us ( const QWidget* candidate ) const
	{
		return ( candidate != nullptr ) && ( ( candidate == scope ) || scope->isAncestorOf ( candidate ) );
	}

	bool FocusHighlight::holds_focus () const
	{
		// Descendant focus counts. A cell editor is a child widget of the view, and while one is open the view itself
		// does NOT hold focus -- but the user is unmistakably working in this grid, and muting the bar at the moment
		// they start typing would be precisely backwards.

		return belongs_to_us ( QApplication::focusWidget () );
	}

	void FocusHighlight::schedule_refresh ()
	{
		// DEFERRED, for two reasons that both come down to reading the theme at the wrong moment.
		//
		//   - ThemeService::apply() installs the style BEFORE the palette, so the StyleChange that brings the news
		//     arrives while QApplication::palette() still holds the OLD theme. Answering it synchronously would write
		//     the outgoing theme's colours and then never hear about the incoming one.
		//   - Setting a palette here would re-enter this filter through PaletteChange (never re-present a view
		//     from inside its own event).
		//
		// One application produces a burst of these; the flag collapses them into a single re-read.

		if ( refreshPending )
		{
			return;
		}

		refreshPending = true;

		QMetaObject::invokeMethod ( this, [ this ] () { refresh (); }, Qt::QueuedConnection );
	}
}

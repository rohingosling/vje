//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ThemeService implementation. Applies the theme by installing the platform-neutral Fusion style and an explicit
//   QPalette, so both themes render the same on Windows and Linux. System resolves the effective light/dark from
//   QStyleHints::colorScheme() and re-applies live on colorSchemeChanged.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "ThemeService.hpp"

#include "style/FluentStyle.hpp"
#include "style/tone.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QStyleFactory>
#include <QStyleHints>

namespace vje
{
	namespace
	{
		// The persisted string forms of the three themes now live with the other stored spellings, in settings_values --
		// the Settings dialog's General group offers the same three (SET-03), so a second statement of them here would be
		// a second opinion. Aliased locally so the mapping functions below stay readable.

		const QString& THEME_LIGHT  = settings_values::THEME_LIGHT;
		const QString& THEME_DARK   = settings_values::THEME_DARK;
		const QString& THEME_SYSTEM = settings_values::THEME_SYSTEM;

		//-------------------------------------------------------------------------------------------------------------
		// STYLE-16 -- a disabled control shades its FILL, not just its text.
		//
		// Both fill roles are derived from their own enabled value rather than named per theme, which is what makes one
		// number lighter on the dark theme and darker on the light one (style/tone.hpp). Both are needed because Qt
		// fills the two families of control from different roles: Base is a text box's edit area, a spin box's, and an
		// editable combo box's, while Button is a drop-down list's face and a push button's.
		//
		// It is deliberately the PALETTE and not FluentStyle: the palette reaches every control without any of them
		// being listed, and FluentStyle's contract is shape and metrics, NEVER colour (architecture.md section 9.1).
		//
		// Read-only controls are untouched by construction -- Qt reads the Disabled colour group from the widget's
		// ENABLED state, and a read-only widget is enabled. That is STYLE-16's decision holding for free rather than
		// by a check somewhere.
		//-------------------------------------------------------------------------------------------------------------

		void apply_disabled_fills ( QPalette& palette )
		{
			const QColor base   = palette.color ( QPalette::Base );
			const QColor button = palette.color ( QPalette::Button );

			palette.setColor
			(
				QPalette::Disabled, QPalette::Base,
				contrasting_tone ( base, config::appearance::DISABLED_FILL_CONTRAST )
			);

			palette.setColor
			(
				QPalette::Disabled, QPalette::Button,
				contrasting_tone ( button, config::appearance::DISABLED_FILL_CONTRAST )
			);
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	ThemeService::ThemeService ( SettingsStore* settings, QObject* parent )
		: QObject  ( parent )
		, settings ( settings )
	{
		// Adopt the persisted choice (default Light) without applying yet -- the composition root applies once the
		// QApplication exists.

		currentTheme = theme_from_string ( settings->value_string ( settings_keys::THEME, THEME_LIGHT ), Theme::Light );

		// Track the OS light/dark preference so a System theme recolours live when the OS switches (Qt 6.5+).

		connect
		(
			QGuiApplication::styleHints (), &QStyleHints::colorSchemeChanged,
			this,                           [ this ] ( Qt::ColorScheme )
			{
				if ( currentTheme == Theme::System )
				{
					apply ();
				}
			}
		);
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	Theme ThemeService::theme () const
	{
		return currentTheme;
	}

	bool ThemeService::is_dark_effective () const
	{
		return darkEffective;
	}

	QString ThemeService::theme_to_string ( Theme theme )
	{
		switch ( theme )
		{
			case Theme::Light:  return THEME_LIGHT;
			case Theme::Dark:   return THEME_DARK;
			case Theme::System: return THEME_SYSTEM;
		}

		return THEME_LIGHT;
	}

	Theme ThemeService::theme_from_string ( const QString& text, Theme fallback )
	{
		if ( text.compare ( THEME_LIGHT,  Qt::CaseInsensitive ) == 0 ) return Theme::Light;
		if ( text.compare ( THEME_DARK,   Qt::CaseInsensitive ) == 0 ) return Theme::Dark;
		if ( text.compare ( THEME_SYSTEM, Qt::CaseInsensitive ) == 0 ) return Theme::System;

		return fallback;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void ThemeService::set_theme ( Theme theme )
	{
		if ( theme == currentTheme )
		{
			return;
		}

		currentTheme = theme;

		settings->set_string ( settings_keys::THEME, theme_to_string ( theme ) );

		apply ();

		emit theme_changed ( currentTheme );
	}

	void ThemeService::set_interface_style ( config::appearance::InterfaceStyle style )
	{
		// Deliberately does NOT apply. The caller decides when the restyle happens, because the composition root sets
		// this before the first apply() (so the very first paint is already correct) while the Settings dialog sets it
		// after the store has changed and then applies once -- and a setter that applied would make the first of those
		// an extra full restyle at startup for no reason.

		interfaceStyle = style;
	}

	void ThemeService::apply ()
	{
		// Fusion is the one Qt style guaranteed on both platforms and fully driven by the palette, so Light and Dark
		// render identically on Windows and Linux. FluentStyle wraps it to carry the Fluent menu metrics and, under
		// SET-12's Fluent interface style, STYLE-05's control radius. It states no colour either way, so every colour
		// still comes from the palette apply_look installs beside it.
		//
		// The interface style is PUSHED IN rather than read from the store here, which is the rule views/Card follows
		// for the same setting's other half -- and the reason is the same in both places, seen from two directions. A
		// Card is a general container that must not know about settings; this service must not depend on
		// settings_profiles, which reaches into vje_core for the format profiles, and dragging that into a class ten
		// test targets compile is a dependency bought for one enum. The composition root reads it once and hands it to
		// both consumers.
		//
		// Rebuilding the style object on every apply() is what makes one call answer both "the theme changed" and "the
		// interface style changed", so the two can never get out of step.

		darkEffective = resolve_dark ();

		apply_look ( interfaceStyle, darkEffective );

		// Every application invalidates palette-derived resources -- notably the tinted icons. This fires on the
		// OS-driven repaint under the System theme too, where theme_changed() does not (the choice has not changed).

		emit applied ();
	}

	void ThemeService::apply_look ( config::appearance::InterfaceStyle style, bool dark )
	{
		if ( QStyle* fusion = QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) )
		{
			QApplication::setStyle ( new FluentStyle ( fusion, style ) );
		}

		QApplication::setPalette ( dark ? dark_palette () : light_palette () );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool ThemeService::resolve_dark () const
	{
		switch ( currentTheme )
		{
			case Theme::Light: return false;
			case Theme::Dark:  return true;

			case Theme::System:

				// An Unknown colorScheme (older desktops) falls back to Light.

				return QGuiApplication::styleHints ()->colorScheme () == Qt::ColorScheme::Dark;
		}

		return false;
	}

	QPalette ThemeService::light_palette ()
	{
		// A clean neutral light palette. The full Fluent card surfaces (STYLE-*) layer on top of this in Phase 14.

		QPalette palette;

		const QColor windowBackground ( 0xF3, 0xF3, 0xF3 );
		const QColor baseBackground   ( 0xFF, 0xFF, 0xFF );
		const QColor alternateBase    ( 0xF7, 0xF7, 0xF7 );
		const QColor text             ( 0x1A, 0x1A, 0x1A );
		const QColor disabledText     ( 0xA0, 0xA0, 0xA0 );
		const QColor highlight        ( 0x00, 0x67, 0xC0 );
		const QColor mutedHighlight   ( 0xE1, 0xE1, 0xE1 );
		const QColor focusedSurface   ( 0xC9, 0xC9, 0xC9 );
		const QColor unfocusedSurface ( 0xE7, 0xE7, 0xE7 );

		palette.setColor ( QPalette::Window,          windowBackground );
		palette.setColor ( QPalette::WindowText,      text );
		palette.setColor ( QPalette::Base,            baseBackground );
		palette.setColor ( QPalette::AlternateBase,   alternateBase );
		palette.setColor ( QPalette::ToolTipBase,     baseBackground );
		palette.setColor ( QPalette::ToolTipText,     text );
		palette.setColor ( QPalette::Text,            text );
		palette.setColor ( QPalette::Button,          focusedSurface );
		palette.setColor ( QPalette::ButtonText,      text );
		palette.setColor ( QPalette::Highlight,       highlight );
		palette.setColor ( QPalette::HighlightedText, QColor ( 0xFF, 0xFF, 0xFF ) );
		palette.setColor ( QPalette::Link,            highlight );

		// The accent, stated. Left unset, it does NOT follow the Highlight above: the palette keeps its default accent,
		// #308CC6 in both themes (measured, Phase 15l -- tst_tree_node_delegate fails without these lines), which is
		// neither theme's blue. TREE-10's dot is drawn in it. A role of its own rather than Highlight, because
		// FocusHighlight rewrites a view's Highlight to the muted bar whenever the view loses the keyboard, and the dot
		// must stay the accent whichever pane holds it. All three groups, so a deactivated window keeps it too.

		palette.setColor ( QPalette::Active,   QPalette::Accent, highlight );
		palette.setColor ( QPalette::Inactive, QPalette::Accent, highlight );
		palette.setColor ( QPalette::Disabled, QPalette::Accent, highlight );
		palette.setColor ( QPalette::PlaceholderText, disabledText );

		palette.setColor ( QPalette::Disabled, QPalette::Text,       disabledText );
		palette.setColor ( QPalette::Disabled, QPalette::ButtonText, disabledText );
		palette.setColor ( QPalette::Disabled, QPalette::WindowText, disabledText );

		// The UNFOCUSED selection bar (STYLE-12). Qt's Inactive group is its natural home, and putting it there buys
		// two things at once: FocusHighlight reads it when a view does not hold keyboard focus, and Qt applies it
		// unaided when the whole window is deactivated -- one definition covering both.
		//
		// The text reverts to the ordinary text colour. Keeping HighlightedText white would leave white-on-light-grey,
		// which is the one combination this bar must never produce.

		palette.setColor ( QPalette::Inactive, QPalette::Highlight,       mutedHighlight );
		palette.setColor ( QPalette::Inactive, QPalette::HighlightedText, text );

		// The UNFOCUSED pane surface. NO LONGER the tab strip or the Explorer band: both now paint from
		// style/tab_surface.hpp, because Fusion always draws an unselected tab darker than the selected one and that is
		// backwards against white content (STYLE-14). Button still reaches every push button, so the pair is kept.
		//
		// The LIGHT theme's focused surface is the DARKER of its two, which is the opposite of the dark theme's and is
		// the point rather than an inconsistency: what the focused pane does is stand away from the content beside it,
		// and on a white content area that means going darker (see the note in dark_palette).
		//
		// Both values are SOLVED rather than picked. Fusion turns Button into a tab fill through a private gradient, so
		// these were found by rendering a tab at each candidate level and reading the pixel back. Restating the fills
		// here would be a second set of numbers to keep in step, so the targets live in the tests that check them.
		//
		// NOTE the reach of QPalette::Button: it is also what every push button is drawn from, so the focused chrome
		// colour is the button colour too. Barely visible in Phase 7 -- only the About and error dialogs carry a button
		// -- and worth looking at again when Phase 10 brings the Settings dialog.

		palette.setColor ( QPalette::Inactive, QPalette::Button, unfocusedSurface );
		// STYLE-16, last so it derives from the enabled fills this function has finished settling.

		apply_disabled_fills ( palette );

		return palette;
	}

	QPalette ThemeService::dark_palette ()
	{
		// The familiar Fusion dark palette, defined explicitly so it is identical on both platforms.

		QPalette palette;

		const QColor windowBackground ( 0x2D, 0x2D, 0x30 );
		const QColor baseBackground   ( 0x25, 0x25, 0x26 );
		const QColor alternateBase    ( 0x2D, 0x2D, 0x30 );
		const QColor text             ( 0xF0, 0xF0, 0xF0 );
		const QColor disabledText     ( 0x7F, 0x7F, 0x7F );
		const QColor focusedSurface   ( 0x37, 0x37, 0x37 );
		const QColor highlight        ( 0x2A, 0x7C, 0xD6 );
		const QColor mutedHighlight   ( 0x3F, 0x3F, 0x46 );
		const QColor unfocusedSurface ( 0x29, 0x29, 0x29 );

		palette.setColor ( QPalette::Window,          windowBackground );
		palette.setColor ( QPalette::WindowText,      text );
		palette.setColor ( QPalette::Base,            baseBackground );
		palette.setColor ( QPalette::AlternateBase,   alternateBase );
		palette.setColor ( QPalette::ToolTipBase,     baseBackground );
		palette.setColor ( QPalette::ToolTipText,     text );
		palette.setColor ( QPalette::Text,            text );
		palette.setColor ( QPalette::Button,          focusedSurface );
		palette.setColor ( QPalette::ButtonText,      text );
		palette.setColor ( QPalette::BrightText,      QColor ( 0xFF, 0x55, 0x55 ) );
		palette.setColor ( QPalette::Highlight,       highlight );
		palette.setColor ( QPalette::HighlightedText, QColor ( 0xFF, 0xFF, 0xFF ) );
		palette.setColor ( QPalette::Link,            QColor ( 0x4A, 0x9C, 0xF0 ) );

		// The accent, as the light palette states it (TREE-10).

		palette.setColor ( QPalette::Active,   QPalette::Accent, highlight );
		palette.setColor ( QPalette::Inactive, QPalette::Accent, highlight );
		palette.setColor ( QPalette::Disabled, QPalette::Accent, highlight );
		palette.setColor ( QPalette::PlaceholderText, disabledText );

		palette.setColor ( QPalette::Disabled, QPalette::Text,       disabledText );
		palette.setColor ( QPalette::Disabled, QPalette::ButtonText, disabledText );
		palette.setColor ( QPalette::Disabled, QPalette::WindowText, disabledText );

		// The unfocused selection bar and pane surface (STYLE-12 / STYLE-14).
		//
		// THE RULE BOTH THEMES FOLLOW, which is easier to see stated once than inferred from four hex values: the
		// FOCUSED pane's chrome stands AWAY from the content beside it, and the unfocused pane's sinks toward it. Here
		// the content is #252526, so standing away means going lighter; in the light theme the content is #FFFFFF, so
		// it means going darker. The two themes therefore move in opposite directions and obey the same rule.

		palette.setColor ( QPalette::Inactive, QPalette::Highlight,       mutedHighlight );
		palette.setColor ( QPalette::Inactive, QPalette::HighlightedText, text );
		palette.setColor ( QPalette::Inactive, QPalette::Button,          unfocusedSurface );
		// STYLE-16, last so it derives from the enabled fills this function has finished settling.

		apply_disabled_fills ( palette );

		return palette;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   ThemeService -- applies and persists the Light / Dark / System theme (LD-7). Light is the default. For System, the OS light/dark preference is read
//   from QStyleHints::colorScheme()
//   (Qt 6.5+) and tracked via colorSchemeChanged, so a live OS theme switch recolours the app.
//
//   Phase 5 establishes the MECHANISM with the platform-neutral Fusion style plus explicit light/dark QPalettes, so
//   the theme visibly applies, persists (settings_keys::THEME), and renders identically on Windows and Linux (no
//   Windows-only material). The full Fluent card-surface QSS and the Code View token palettes are Phase 14 (STYLE-*),
//   layered on top of this same service.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QObject>
#include <QPalette>
#include <QString>

namespace vje
{
	class SettingsStore;

	//-----------------------------------------------------------------------------------------------------------------
	// The user's theme choice. System resolves to Light or Dark from the OS at apply time.
	//-----------------------------------------------------------------------------------------------------------------

	enum class Theme
	{
		Light,
		Dark,
		System
	};

	//*****************************************************************************************************************
	// Class: ThemeService
	//*****************************************************************************************************************

	class ThemeService : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Reads the persisted choice from the store (default Light). Does not apply yet -- the composition root calls
		// apply() once the QApplication exists.

		explicit ThemeService ( SettingsStore* settings, QObject* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		Theme theme             () const;   // The chosen setting (may be System).
		bool  is_dark_effective () const;   // The resolved light/dark actually applied.

		static QString theme_to_string   ( Theme theme );
		static Theme   theme_from_string ( const QString& text, Theme fallback = Theme::Light );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_theme ( Theme theme );     // Persists, applies, and emits theme_changed on a real change.
		void apply     ();                  // (Re)apply the current theme to the QApplication.

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void theme_changed ( Theme theme );   // The user's choice changed.

		// A palette was (re)applied. Fires on every apply(), including the OS-driven repaint under the System theme
		// where the choice itself is unchanged -- so palette-derived resources (IconLibrary's tinted icons) rebuild.

		void applied ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		bool resolve_dark () const;         // System -> OS colorScheme; else the chosen theme.

		static QPalette light_palette ();
		static QPalette dark_palette  ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		SettingsStore* settings;            // Non-owning.
		Theme          currentTheme  = Theme::Light;
		bool           darkEffective = false;
	};
}

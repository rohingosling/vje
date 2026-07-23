//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   fixed_font implementation. See the header for what QFontDatabase::systemFont(FixedFont) actually returns and why
//   the choice has to be measured.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/fixed_font.hpp"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QStringList>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Concrete families in preference order, covering both target platforms and then some. Deliberately NOT in
		// AppConfig: this is a single-purpose list that only means anything next to the measurement below, and the
		// header's rule about single-call-site values applies.
		//
		// Every entry is a real family name. The generic aliases ("monospace", "Monospace") are the thing that caused
		// the problem and appear nowhere.
		//-------------------------------------------------------------------------------------------------------------

		const QStringList& candidate_families ()
		{
			static const QStringList families =
			{
				QStringLiteral ( "Consolas" ),            // Windows, since Vista.
				QStringLiteral ( "Cascadia Mono" ),       // Windows 11 / modern terminal default.
				QStringLiteral ( "DejaVu Sans Mono" ),    // The Linux Mint / Ubuntu default.
				QStringLiteral ( "Liberation Mono" ),     // Widely installed on Linux where DejaVu is not.
				QStringLiteral ( "Menlo" ),               // macOS, for the later stretch-goal platform.
				QStringLiteral ( "Courier New" )          // The universal last resort; present nearly everywhere.
			};

			return families;
		}
	}

	bool measures_fixed_width ( const QFont& font )
	{
		const QFontMetricsF metrics ( font );

		// Two pairs at opposite extremes of a proportional face's width range. One pair would be enough for any real
		// font; two costs nothing and closes the door on a face that happens to draw 'i' and 'W' alike.

		return qFuzzyCompare ( metrics.horizontalAdvance ( QLatin1Char ( 'i' ) ), metrics.horizontalAdvance ( QLatin1Char ( 'W' ) ) )
		    && qFuzzyCompare ( metrics.horizontalAdvance ( QLatin1Char ( '.' ) ), metrics.horizontalAdvance ( QLatin1Char ( 'm' ) ) );
	}

	QFont monospace_font ()
	{
		QFont baseFont = QFontDatabase::systemFont ( QFontDatabase::FixedFont );

		// Both are requests to the matcher rather than guarantees -- kept because they cost nothing and improve what a
		// fallback resolves to, but neither is trusted: the measurement below is what decides.

		baseFont.setStyleHint ( QFont::Monospace );
		baseFont.setFixedPitch ( true );

		if ( measures_fixed_width ( baseFont ) )
		{
			return baseFont;
		}

		for ( const QString& family : candidate_families () )
		{
			QFont candidate = baseFont;

			candidate.setFamily ( family );

			if ( measures_fixed_width ( candidate ) )
			{
				return candidate;
			}
		}

		// Nothing on this machine measures fixed. Returning the system's answer keeps the text readable, if misaligned,
		// which is the better of two bad outcomes.

		return baseFont;
	}
}

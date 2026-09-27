//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   explorer_integration, every platform -- the registration plan and the removal order, which are pure and so are
//   compiled everywhere (see the header). The operations live in one file per platform, chosen by CMake.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/explorer_integration.hpp>

#include <QStringList>

#include <algorithm>

namespace vje::platform::explorer_integration
{
	namespace
	{
		constexpr QChar KEY_SEPARATOR  = QLatin1Char ( '\\' );
		constexpr QChar PATH_SEPARATOR = QLatin1Char ( '\\' );   // Windows'; the plan is Windows data on every host.

		// What each entry displays. "VJE" is what Open with lists; the verb's text is what the classic menu shows.

		const QString OPEN_WITH_NAME = QStringLiteral ( "VJE" );
		const QString EDIT_VERB_TEXT = QStringLiteral ( "Edit in VJE" );
		const QString PROG_ID_TYPE   = QStringLiteral ( "JSON File" );

		QString key_path ( std::initializer_list<QString> parts )
		{
			QStringList joined;

			for ( const QString& part : parts )
			{
				joined.append ( part );
			}

			return joined.join ( KEY_SEPARATOR );
		}
	}

	std::vector<RegistryValue> registration_plan ( const QString& windowProgramPath )
	{
		// QUOTED, so a path with a space in it -- "Program Files" -- is one argument. "%1" is the file Explorer hands
		// over, quoted for the same reason. Both entries run the WINDOW program: Explorer never launches the console
		// twin, so no console opens beside VJE (CLI-07).
		//
		// BACKSLASHES BY HAND, not QDir::toNativeSeparators: that converts to the HOST's separator, so on Linux -- where
		// this pure plan is also built and tested -- it left Qt's forward slashes in what is Windows registry data. The
		// Linux run of the plan's suite caught it (2026-09-26).

		const QString windowsPath = QString ( windowProgramPath ).replace ( QLatin1Char ( '/' ), PATH_SEPARATOR );
		const QString program     = QLatin1Char ( '"' ) + windowsPath + QLatin1Char ( '"' );
		const QString command     = program + QStringLiteral ( " \"%1\"" );
		const QString icon        = program + QStringLiteral ( ",0" );

		// The keys, relative to the classes root.

		const QString progIdIcon  = key_path ( { PROG_ID, QStringLiteral ( "DefaultIcon" ) } );
		const QString openVerb    = key_path ( { PROG_ID, QStringLiteral ( "shell" ), QStringLiteral ( "open" ) } );
		const QString openCommand = key_path ( { openVerb, QStringLiteral ( "command" ) } );
		const QString openWith    = key_path ( { EXTENSION, QStringLiteral ( "OpenWithProgids" ) } );
		const QString editVerb    = key_path ( { QStringLiteral ( "SystemFileAssociations" ), EXTENSION, QStringLiteral ( "shell" ), EDIT_VERB } );
		const QString editCommand = key_path ( { editVerb, QStringLiteral ( "command" ) } );

		const QString defaultValue;                        // The key's unnamed value.

		return
		{
			// Open with > VJE: a ProgID of VJE's own, and the one value VJE adds to .json's OpenWithProgids -- never
			// .json's default value, which is what would make VJE the default application.

			{ PROG_ID,     defaultValue,                          PROG_ID_TYPE   },
			{ progIdIcon,  defaultValue,                          icon           },
			{ openVerb,    QStringLiteral ( "FriendlyAppName" ),  OPEN_WITH_NAME },
			{ openCommand, defaultValue,                          command        },
			{ openWith,    PROG_ID,                               QString ()     },

			// Edit in VJE, behind Show more options.

			{ editVerb,    defaultValue,                          EDIT_VERB_TEXT },
			{ editVerb,    QStringLiteral ( "Icon" ),             icon           },
			{ editCommand, defaultValue,                          command        }
		};
	}

	std::vector<QString> removal_order ( const std::vector<RegistryValue>& plan )
	{
		// Every key on every value's path -- the key itself and each ancestor below the classes root -- once.

		std::vector<QString> keys;

		for ( const RegistryValue& value : plan )
		{
			QString key = value.key;

			while ( !key.isEmpty () )
			{
				if ( std::find ( keys.begin (), keys.end (), key ) == keys.end () )
				{
					keys.push_back ( key );
				}

				const qsizetype separator = key.lastIndexOf ( KEY_SEPARATOR );

				key = ( separator < 0 ) ? QString () : key.left ( separator );
			}
		}

		// Deepest first, so a parent is examined only after everything under it has had its chance to go. Stable, so
		// keys of one depth keep the plan's order and the result is the same on every run.

		std::stable_sort ( keys.begin (), keys.end (), [] ( const QString& left, const QString& right )
		{
			return left.count ( KEY_SEPARATOR ) > right.count ( KEY_SEPARATOR );
		} );

		return keys;
	}
}

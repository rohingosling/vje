//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   explorer_integration, Windows -- the plan applied to and removed from HKEY_CURRENT_USER (see the header).
//
//   NOTHING HERE DECIDES WHAT IS WRITTEN. The values come from registration_plan and the keys removal considers from
//   removal_order, both pure and both tested headlessly; this file only turns them into registry calls. So the one
//   question a reader of it has to answer is whether each call does what its name says.
//
//   ONLY THE LIVE ROOT NOTIFIES EXPLORER. SHChangeNotify ( SHCNE_ASSOCCHANGED ) is what makes both menus change without
//   a sign-out, and it makes the shell re-read every association -- which a test writing to a scratch root has no
//   business causing.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/explorer_integration.hpp>

#include <windows.h>
#include <shlobj.h>

namespace vje::platform::explorer_integration
{
	namespace
	{
		std::wstring wide ( const QString& text )
		{
			return text.toStdWString ();
		}

		QString full_key ( const QString& classesRoot, const QString& key )
		{
			return classesRoot + QLatin1Char ( '\\' ) + key;
		}

		// The system's own sentence for a registry error, so a failure names its cause ("Access is denied.").

		QString system_message ( LSTATUS status )
		{
			wchar_t* buffer = nullptr;

			const DWORD length = FormatMessageW
			(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr,
				static_cast<DWORD> ( status ),
				0,
				reinterpret_cast<wchar_t*> ( &buffer ),
				0,
				nullptr
			);

			QString message = ( length > 0 ) ? QString::fromWCharArray ( buffer, static_cast<qsizetype> ( length ) ).trimmed ()
			                                 : QStringLiteral ( "error %1" ).arg ( status );

			LocalFree ( buffer );

			return message;
		}

		Outcome failure ( const QString& action, const QString& key, LSTATUS status )
		{
			return { false, QStringLiteral ( "Could not %1 HKEY_CURRENT_USER\\%2: %3" ).arg ( action, key, system_message ( status ) ) };
		}

		bool is_live ( const QString& classesRoot )
		{
			return classesRoot.compare ( LIVE_CLASSES_ROOT, Qt::CaseInsensitive ) == 0;
		}

		void notify_explorer ( const QString& classesRoot )
		{
			if ( is_live ( classesRoot ) )
			{
				SHChangeNotify ( SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr );
			}
		}

		// A value's data as REG_SZ, or nothing when the value is absent or of another type.

		bool read_string ( const QString& key, const QString& name, QString& data )
		{
			const std::wstring keyText  = wide ( key );
			const std::wstring nameText = wide ( name );

			DWORD size = 0;

			if ( RegGetValueW ( HKEY_CURRENT_USER, keyText.c_str (), nameText.c_str (), RRF_RT_REG_SZ, nullptr, nullptr, &size ) != ERROR_SUCCESS )
			{
				return false;
			}

			std::wstring buffer ( size / sizeof ( wchar_t ) + 1, L'\0' );

			if ( RegGetValueW ( HKEY_CURRENT_USER, keyText.c_str (), nameText.c_str (), RRF_RT_REG_SZ, nullptr, buffer.data (), &size ) != ERROR_SUCCESS )
			{
				return false;
			}

			data = QString::fromWCharArray ( buffer.c_str () );

			return true;
		}

		bool value_exists ( const QString& key, const QString& name )
		{
			const std::wstring keyText  = wide ( key );
			const std::wstring nameText = wide ( name );

			return RegGetValueW ( HKEY_CURRENT_USER, keyText.c_str (), nameText.c_str (), RRF_RT_ANY, nullptr, nullptr, nullptr ) == ERROR_SUCCESS;
		}

		Outcome write_value ( const QString& key, const RegistryValue& value )
		{
			const std::wstring keyText  = wide ( key );
			const std::wstring nameText = wide ( value.name );
			const std::wstring dataText = wide ( value.data );

			HKEY handle = nullptr;

			const LSTATUS created = RegCreateKeyExW ( HKEY_CURRENT_USER, keyText.c_str (), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &handle, nullptr );

			if ( created != ERROR_SUCCESS )
			{
				return failure ( QStringLiteral ( "create" ), key, created );
			}

			// The size counts the terminating null, as REG_SZ requires.

			const DWORD   bytes = static_cast<DWORD> ( ( dataText.size () + 1 ) * sizeof ( wchar_t ) );
			const LSTATUS set   = RegSetValueExW ( handle, nameText.c_str (), 0, REG_SZ, reinterpret_cast<const BYTE*> ( dataText.c_str () ), bytes );

			RegCloseKey ( handle );

			return ( set == ERROR_SUCCESS ) ? Outcome {} : failure ( QStringLiteral ( "write to" ), key, set );
		}

		// Delete one value. Absent -- the value, or the key holding it -- is success: there was nothing to remove.

		Outcome delete_value ( const QString& key, const QString& name )
		{
			const std::wstring keyText  = wide ( key );
			const std::wstring nameText = wide ( name );

			const LSTATUS status = RegDeleteKeyValueW ( HKEY_CURRENT_USER, keyText.c_str (), nameText.c_str () );

			if ( ( status == ERROR_SUCCESS ) || ( status == ERROR_FILE_NOT_FOUND ) )
			{
				return {};
			}

			return failure ( QStringLiteral ( "remove a value from" ), key, status );
		}

		// Delete a key if, and only if, it holds no value and no subkey. Absent is success.

		Outcome delete_key_if_empty ( const QString& key )
		{
			const std::wstring keyText = wide ( key );

			HKEY handle = nullptr;

			const LSTATUS opened = RegOpenKeyExW ( HKEY_CURRENT_USER, keyText.c_str (), 0, KEY_QUERY_VALUE, &handle );

			if ( opened == ERROR_FILE_NOT_FOUND )
			{
				return {};
			}

			if ( opened != ERROR_SUCCESS )
			{
				return failure ( QStringLiteral ( "open" ), key, opened );
			}

			DWORD subkeys = 0;
			DWORD values  = 0;

			const LSTATUS queried = RegQueryInfoKeyW ( handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr );

			RegCloseKey ( handle );

			if ( queried != ERROR_SUCCESS )
			{
				return failure ( QStringLiteral ( "examine" ), key, queried );
			}

			if ( ( subkeys > 0 ) || ( values > 0 ) )
			{
				return {};
			}

			const LSTATUS deleted = RegDeleteKeyW ( HKEY_CURRENT_USER, keyText.c_str () );

			return ( ( deleted == ERROR_SUCCESS ) || ( deleted == ERROR_FILE_NOT_FOUND ) ) ? Outcome {} : failure ( QStringLiteral ( "remove" ), key, deleted );
		}

		// Removal without the notification, so a failed registration can roll back and report its own failure.

		Outcome remove_plan ( const QString& classesRoot )
		{
			const std::vector<RegistryValue> plan = registration_plan ( QString () );

			for ( const RegistryValue& value : plan )
			{
				const Outcome removed = delete_value ( full_key ( classesRoot, value.key ), value.name );

				if ( !removed.succeeded )
				{
					return removed;
				}
			}

			for ( const QString& key : removal_order ( plan ) )
			{
				const Outcome removed = delete_key_if_empty ( full_key ( classesRoot, key ) );

				if ( !removed.succeeded )
				{
					return removed;
				}
			}

			return {};
		}
	}

	bool is_supported ()
	{
		return true;
	}

	Outcome register_for ( const QString& windowProgramPath, const QString& classesRoot )
	{
		for ( const RegistryValue& value : registration_plan ( windowProgramPath ) )
		{
			const Outcome written = write_value ( full_key ( classesRoot, value.key ), value );

			if ( !written.succeeded )
			{
				// No half of a registration is left behind. The removal's own outcome is not reported over the write's:
				// the write is what the user asked for, and its failure is the one that explains what happened.

				remove_plan ( classesRoot );

				notify_explorer ( classesRoot );

				return written;
			}
		}

		notify_explorer ( classesRoot );

		return {};
	}

	Outcome unregister ( const QString& classesRoot )
	{
		const Outcome removed = remove_plan ( classesRoot );

		notify_explorer ( classesRoot );

		return removed;
	}

	bool is_registered ( const QString& classesRoot )
	{
		for ( const RegistryValue& value : registration_plan ( QString () ) )
		{
			if ( value_exists ( full_key ( classesRoot, value.key ), value.name ) )
			{
				return true;
			}
		}

		return false;
	}

	bool is_registered_for ( const QString& windowProgramPath, const QString& classesRoot )
	{
		for ( const RegistryValue& value : registration_plan ( windowProgramPath ) )
		{
			QString data;

			if ( !read_string ( full_key ( classesRoot, value.key ), value.name, data ) || ( data != value.data ) )
			{
				return false;
			}
		}

		return true;
	}
}

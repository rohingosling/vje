//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   SettingsStore implementation. Backed by a single flat QJsonObject persisted as pretty-printed UTF-8 JSON; reads
//   are tolerant (wrong type / absent -> default) and writes are immediate and deduped.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "SettingsStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

namespace vje
{
	namespace
	{
		// The reserved member that records the on-disk schema version, kept out of the caller's dot-namespaced key
		// space so it can never collide with a real setting.

		const QString SCHEMA_VERSION_KEY = QStringLiteral ( "schemaVersion" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	SettingsStore::SettingsStore ( const QString& filePath, QObject* parent )
		: QObject       ( parent )
		, storeFilePath ( filePath )
	{
		load ();
	}

	QString SettingsStore::default_file_path ()
	{
		// AppConfigLocation is the platform's standard per-user config dir (NFR-06). It is derived from the
		// application/organization name set on QCoreApplication by the composition root.

		const QString configDirectory = QStandardPaths::writableLocation ( QStandardPaths::AppConfigLocation );

		return configDirectory + QStringLiteral ( "/settings.json" );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	const QString& SettingsStore::file_path () const
	{
		return storeFilePath;
	}

	bool SettingsStore::contains ( const QString& key ) const
	{
		return values.contains ( key );
	}

	bool SettingsStore::value_bool ( const QString& key, bool defaultValue ) const
	{
		const QJsonValue value = values.value ( key );

		return value.isBool () ? value.toBool () : defaultValue;
	}

	int SettingsStore::value_int ( const QString& key, int defaultValue ) const
	{
		const QJsonValue value = values.value ( key );

		return value.isDouble () ? value.toInt ( defaultValue ) : defaultValue;
	}

	QString SettingsStore::value_string ( const QString& key, const QString& defaultValue ) const
	{
		const QJsonValue value = values.value ( key );

		return value.isString () ? value.toString () : defaultValue;
	}

	QStringList SettingsStore::value_string_list ( const QString& key ) const
	{
		const QJsonValue value = values.value ( key );

		if ( !value.isArray () )
		{
			return QStringList ();
		}

		// Take only the string entries; a malformed (non-string) entry is skipped rather than failing the whole read.

		QStringList result;

		for ( const QJsonValue& entry : value.toArray () )
		{
			if ( entry.isString () )
			{
				result.append ( entry.toString () );
			}
		}

		return result;
	}

	QByteArray SettingsStore::value_bytes ( const QString& key ) const
	{
		const QJsonValue value = values.value ( key );

		return value.isString () ? QByteArray::fromBase64 ( value.toString ().toLatin1 () ) : QByteArray ();
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	bool SettingsStore::set_bool ( const QString& key, bool value )
	{
		return store_value ( key, QJsonValue ( value ) );
	}

	bool SettingsStore::set_int ( const QString& key, int value )
	{
		return store_value ( key, QJsonValue ( value ) );
	}

	bool SettingsStore::set_string ( const QString& key, const QString& value )
	{
		return store_value ( key, QJsonValue ( value ) );
	}

	bool SettingsStore::set_string_list ( const QString& key, const QStringList& value )
	{
		QJsonArray array;

		for ( const QString& entry : value )
		{
			array.append ( entry );
		}

		return store_value ( key, QJsonValue ( array ) );
	}

	bool SettingsStore::set_bytes ( const QString& key, const QByteArray& value )
	{
		// Stored as Base64 text so the settings file stays human-readable and valid JSON.

		return store_value ( key, QJsonValue ( QString::fromLatin1 ( value.toBase64 () ) ) );
	}

	void SettingsStore::remove ( const QString& key )
	{
		if ( !values.contains ( key ) )
		{
			return;
		}

		values.remove ( key );
		save ();

		emit changed ( key );
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	bool SettingsStore::save () const
	{
		// Ensure the target directory exists (the standard config dir may not have been created yet).

		const QFileInfo fileInfo ( storeFilePath );

		if ( !QDir ().mkpath ( fileInfo.absolutePath () ) )
		{
			return false;
		}

		// Write atomically via QSaveFile so a crash mid-write cannot corrupt an existing settings file.

		QSaveFile file ( storeFilePath );

		if ( !file.open ( QIODevice::WriteOnly | QIODevice::Text ) )
		{
			return false;
		}

		const QJsonDocument document ( values );

		file.write ( document.toJson ( QJsonDocument::Indented ) );

		return file.commit ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void SettingsStore::load ()
	{
		values = QJsonObject ();

		// A missing file is not an error: start empty and stamp the current schema version below.

		QFile file ( storeFilePath );

		if ( file.exists () && file.open ( QIODevice::ReadOnly | QIODevice::Text ) )
		{
			const QByteArray    contents = file.readAll ();
			const QJsonDocument document = QJsonDocument::fromJson ( contents );

			// A malformed or non-object file is tolerated -- we simply discard it and start from an empty store.

			if ( document.isObject () )
			{
				values = document.object ();
			}
		}

		// Always carry a current schema stamp so a freshly written file is self-describing. (Future incompatible
		// changes bump SCHEMA_VERSION and migrate here; tolerant reads already absorb additive changes.)

		values.insert ( SCHEMA_VERSION_KEY, SCHEMA_VERSION );
	}

	bool SettingsStore::store_value ( const QString& key, const QJsonValue& value )
	{
		// Dedupe: an identical write neither rewrites the file nor emits a change.

		if ( values.contains ( key ) && ( values.value ( key ) == value ) )
		{
			return false;
		}

		values.insert ( key, value );
		save ();

		emit changed ( key );

		return true;
	}
}

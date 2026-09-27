//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include <vje_settings/SettingsStore.hpp>

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

		//-------------------------------------------------------------------------------------------------------------
		// Allman-style JSON output.
		//
		// WHY THIS IS WRITTEN HERE RATHER THAN REUSED. vje_core::JsonFormatter already emits Allman JSON and is the
		// obvious candidate, and it is the wrong one twice over. It formats a JsonNode DOCUMENT through the SET-07
		// format profile, and FILE-03 makes a byte-for-byte claim about that profile against what the Code View shows
		// -- a user-facing contract that the settings file has no part in and must not be able to disturb. And
		// reaching it from here would put vje_core into the link line of the TWENTY-THREE test targets that compile
		// this file, none of which links it today (the same dependency trap ThemeService hit on 2026-08-03).
		//
		// So the two are separate on purpose: one formats the user's documents and answers to a setting, the other
		// writes one internal file in one fixed style. What is NOT restated is JSON syntax itself -- see leaf_token.
		//-------------------------------------------------------------------------------------------------------------

		constexpr int INDENT_WIDTH = 4;   // What Qt's own indented output used, kept so only the BRACING changes.

		QByteArray indent_text ( int depth )
		{
			return QByteArray ( depth * INDENT_WIDTH, ' ' );
		}

		// Qt's own serialization of ONE leaf value -- string escaping, number formatting, true / false / null --
		// borrowed by wrapping the value in an array and taking what comes back between the brackets.
		//
		// Hand-rolling the escaper would be a second opinion about JSON syntax, and this file is full of exactly the
		// cases that punish one: Windows paths carrying backslashes, a base64 window geometry, and whatever a user
		// types into the log folder or the XML text key.

		QByteArray leaf_token ( const QJsonValue& value )
		{
			QJsonArray wrapper;

			wrapper.append ( value );

			const QByteArray text = QJsonDocument ( wrapper ).toJson ( QJsonDocument::Compact );

			const int start = text.indexOf ( '[' ) + 1;
			const int end   = text.lastIndexOf ( ']' );

			return ( ( start > 0 ) && ( end > start ) ) ? text.mid ( start, end - start ) : QByteArray ( "null" );
		}

		// A container with something in it -- the only thing that earns a brace on its own line. An EMPTY object or
		// array stays inline as {} or [], because two lines to say "nothing here" is noise rather than structure.

		bool is_block ( const QJsonValue& value )
		{
			if ( value.isObject () ) { return !value.toObject ().isEmpty (); }
			if ( value.isArray ()  ) { return !value.toArray ().isEmpty ();  }

			return false;
		}

		void write_container ( const QJsonValue& value, int depth, QByteArray& out );

		// Anything that renders inline: a scalar, or an empty container.

		void write_leaf ( const QJsonValue& value, QByteArray& out )
		{
			if      ( value.isObject () ) { out += "{}"; }
			else if ( value.isArray ()  ) { out += "[]"; }
			else                          { out += leaf_token ( value ); }
		}

		// The value half of a member. `depth` is the KEY's depth, which is where an Allman brace belongs.

		void write_member_value ( const QJsonValue& value, int depth, QByteArray& out )
		{
			if ( is_block ( value ) )
			{
				out += "\n";
				out += indent_text ( depth );

				write_container ( value, depth, out );
			}
			else
			{
				out += " ";

				write_leaf ( value, out );
			}
		}

		void write_object ( const QJsonObject& object, int depth, QByteArray& out )
		{
			out += "{\n";

			// QJsonObject iterates its keys in sorted order, which is why the file has always been alphabetical and
			// stays so here -- a stable order is what makes a settings file diffable between sessions.

			const QStringList keys = object.keys ();

			for ( int index = 0; index < keys.size (); ++index )
			{
				out += indent_text ( depth + 1 );
				out += leaf_token ( QJsonValue ( keys [ index ] ) );
				out += ":";

				write_member_value ( object.value ( keys [ index ] ), depth + 1, out );

				out += ( index + 1 < keys.size () ) ? ",\n" : "\n";
			}

			out += indent_text ( depth );
			out += "}";
		}

		void write_array ( const QJsonArray& array, int depth, QByteArray& out )
		{
			out += "[\n";

			for ( int index = 0; index < array.size (); ++index )
			{
				out += indent_text ( depth + 1 );

				if ( is_block ( array.at ( index ) ) )
				{
					write_container ( array.at ( index ), depth + 1, out );
				}
				else
				{
					write_leaf ( array.at ( index ), out );
				}

				out += ( index + 1 < array.size () ) ? ",\n" : "\n";
			}

			out += indent_text ( depth );
			out += "]";
		}

		void write_container ( const QJsonValue& value, int depth, QByteArray& out )
		{
			if ( value.isObject () )
			{
				write_object ( value.toObject (), depth, out );
			}
			else
			{
				write_array ( value.toArray (), depth, out );
			}
		}

		// The whole document. Recursive rather than flat, even though the store is documented as flat, because the
		// header also says it may carry internal settings the dialog does not present -- and a nested one arriving
		// later should format correctly rather than fall out of the one shape this understood.

		QByteArray to_allman_json ( const QJsonObject& root )
		{
			QByteArray out;

			write_object ( root, 0, out );

			out += "\n";

			return out;
		}
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

		// Allman-style bracing, which Qt's own indented output cannot produce -- QJsonDocument::Indented is K&R and
		// offers no choice. The CONTENT is unchanged, so a file written by an older build still loads and a file
		// written by this one still loads anywhere: the difference is whitespace between tokens.

		file.write ( to_allman_json ( values ) );

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

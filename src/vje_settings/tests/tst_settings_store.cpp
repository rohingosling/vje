//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for SettingsStore: typed round-trips through a temp file, persistence across
//   store instances (the NFR-06 promise), tolerant reads (missing key / wrong stored type -> default), the no-op-write
//   dedupe (no rewrite, no signal), removal, the schema-version stamp, and tolerance of a missing or malformed file.
//   Headless -- SettingsStore is Qt Core only.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace vje;

class TestSettingsStore : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void round_trips_typed_values ();
	void persists_across_instances ();
	void missing_key_returns_default ();
	void wrong_type_returns_default ();
	void string_list_skips_non_strings ();
	void bytes_round_trip ();
	void duplicate_write_is_noop ();
	void change_write_emits_signal ();
	void remove_deletes_key ();
	void stamps_schema_version ();
	void tolerates_missing_file ();
	void tolerates_malformed_file ();

	// The on-disk shape (2026-08-06).

	void writes_allman_braced_json ();
	void allman_output_still_round_trips ();

private:

	QString store_path () const;

	QTemporaryDir temporaryDirectory;
};

QString TestSettingsStore::store_path () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/settings.json" );
}

void TestSettingsStore::init ()
{
	QVERIFY ( temporaryDirectory.isValid () );
}

void TestSettingsStore::cleanup ()
{
	// Start each case from a clean file.

	QFile::remove ( store_path () );
}

void TestSettingsStore::round_trips_typed_values ()
{
	SettingsStore store ( store_path () );

	store.set_bool        ( QStringLiteral ( "a.flag" ),   true );
	store.set_int         ( QStringLiteral ( "a.count" ),  42 );
	store.set_string      ( QStringLiteral ( "a.name" ),   QStringLiteral ( "hello" ) );
	store.set_string_list ( QStringLiteral ( "a.recent" ), { QStringLiteral ( "x" ), QStringLiteral ( "y" ) } );

	QCOMPARE ( store.value_bool        ( QStringLiteral ( "a.flag" ),  false ), true );
	QCOMPARE ( store.value_int         ( QStringLiteral ( "a.count" ), 0 ),     42 );
	QCOMPARE ( store.value_string      ( QStringLiteral ( "a.name" ),  QString () ), QStringLiteral ( "hello" ) );
	QCOMPARE ( store.value_string_list ( QStringLiteral ( "a.recent" ) ), ( QStringList { QStringLiteral ( "x" ), QStringLiteral ( "y" ) } ) );
}

void TestSettingsStore::persists_across_instances ()
{
	{
		SettingsStore writer ( store_path () );
		writer.set_string ( QStringLiteral ( "general.theme" ), QStringLiteral ( "Dark" ) );
		writer.set_int    ( QStringLiteral ( "window.width" ),  1280 );
	}

	// A fresh store over the same file sees the persisted values (NFR-06).

	SettingsStore reader ( store_path () );

	QCOMPARE ( reader.value_string ( QStringLiteral ( "general.theme" ), QStringLiteral ( "Light" ) ), QStringLiteral ( "Dark" ) );
	QCOMPARE ( reader.value_int    ( QStringLiteral ( "window.width" ),  0 ), 1280 );
}

void TestSettingsStore::missing_key_returns_default ()
{
	SettingsStore store ( store_path () );

	QCOMPARE ( store.value_bool   ( QStringLiteral ( "absent" ), true ), true );
	QCOMPARE ( store.value_int    ( QStringLiteral ( "absent" ), 7 ),    7 );
	QCOMPARE ( store.value_string ( QStringLiteral ( "absent" ), QStringLiteral ( "d" ) ), QStringLiteral ( "d" ) );
	QVERIFY  ( store.value_string_list ( QStringLiteral ( "absent" ) ).isEmpty () );
	QVERIFY  ( store.value_bytes ( QStringLiteral ( "absent" ) ).isEmpty () );
	QVERIFY  ( !store.contains ( QStringLiteral ( "absent" ) ) );
}

void TestSettingsStore::wrong_type_returns_default ()
{
	SettingsStore store ( store_path () );

	// A string stored where a bool/int is later requested must fall back to the default, not coerce.

	store.set_string ( QStringLiteral ( "k" ), QStringLiteral ( "not-a-number" ) );

	QCOMPARE ( store.value_bool ( QStringLiteral ( "k" ), true ), true );
	QCOMPARE ( store.value_int  ( QStringLiteral ( "k" ), 99 ),   99 );

	// An int requested as a string likewise falls back.

	store.set_int ( QStringLiteral ( "n" ), 5 );

	QCOMPARE ( store.value_string ( QStringLiteral ( "n" ), QStringLiteral ( "fallback" ) ), QStringLiteral ( "fallback" ) );
}

void TestSettingsStore::string_list_skips_non_strings ()
{
	// Hand-write a mixed array to prove a malformed entry is skipped, not fatal.

	QJsonObject object;
	object.insert ( QStringLiteral ( "mixed" ), QJsonArray { QStringLiteral ( "a" ), 3, QStringLiteral ( "b" ) } );

	QFile file ( store_path () );
	QVERIFY ( file.open ( QIODevice::WriteOnly ) );
	file.write ( QJsonDocument ( object ).toJson () );
	file.close ();

	SettingsStore store ( store_path () );

	QCOMPARE ( store.value_string_list ( QStringLiteral ( "mixed" ) ), ( QStringList { QStringLiteral ( "a" ), QStringLiteral ( "b" ) } ) );
}

void TestSettingsStore::bytes_round_trip ()
{
	SettingsStore store ( store_path () );

	const QByteArray payload = QByteArray::fromHex ( "00010203fffe" );

	store.set_bytes ( QStringLiteral ( "window.geometry" ), payload );

	QCOMPARE ( store.value_bytes ( QStringLiteral ( "window.geometry" ) ), payload );

	// And it survives a reload as Base64 text.

	SettingsStore reloaded ( store_path () );
	QCOMPARE ( reloaded.value_bytes ( QStringLiteral ( "window.geometry" ) ), payload );
}

void TestSettingsStore::duplicate_write_is_noop ()
{
	SettingsStore store ( store_path () );

	QVERIFY ( store.set_int ( QStringLiteral ( "k" ), 1 ) );    // First write changes the value.
	QVERIFY ( !store.set_int ( QStringLiteral ( "k" ), 1 ) );   // Identical write is a no-op.
	QVERIFY ( store.set_int ( QStringLiteral ( "k" ), 2 ) );    // A different value writes again.
}

void TestSettingsStore::change_write_emits_signal ()
{
	SettingsStore store ( store_path () );

	QSignalSpy spy ( &store, &SettingsStore::changed );

	store.set_string ( QStringLiteral ( "general.theme" ), QStringLiteral ( "Dark" ) );
	QCOMPARE ( spy.count (), 1 );
	QCOMPARE ( spy.takeFirst ().at ( 0 ).toString (), QStringLiteral ( "general.theme" ) );

	// A no-op write emits nothing.

	store.set_string ( QStringLiteral ( "general.theme" ), QStringLiteral ( "Dark" ) );
	QCOMPARE ( spy.count (), 0 );
}

void TestSettingsStore::remove_deletes_key ()
{
	SettingsStore store ( store_path () );

	store.set_int ( QStringLiteral ( "k" ), 1 );
	QVERIFY ( store.contains ( QStringLiteral ( "k" ) ) );

	store.remove ( QStringLiteral ( "k" ) );
	QVERIFY ( !store.contains ( QStringLiteral ( "k" ) ) );

	// The removal persists.

	SettingsStore reloaded ( store_path () );
	QVERIFY ( !reloaded.contains ( QStringLiteral ( "k" ) ) );
}

void TestSettingsStore::stamps_schema_version ()
{
	SettingsStore store ( store_path () );
	store.set_int ( QStringLiteral ( "k" ), 1 );   // Force a write.

	QFile file ( store_path () );
	QVERIFY ( file.open ( QIODevice::ReadOnly ) );
	const QJsonObject object = QJsonDocument::fromJson ( file.readAll () ).object ();

	QCOMPARE ( object.value ( QStringLiteral ( "schemaVersion" ) ).toInt ( -1 ), SettingsStore::SCHEMA_VERSION );
}

void TestSettingsStore::tolerates_missing_file ()
{
	// No file on disk: the store is usable and simply returns defaults.

	QVERIFY ( !QFile::exists ( store_path () ) );

	SettingsStore store ( store_path () );

	QCOMPARE ( store.value_int ( QStringLiteral ( "k" ), 3 ), 3 );
}

void TestSettingsStore::tolerates_malformed_file ()
{
	// Garbage (and a valid-JSON-but-not-object file) is discarded; the store starts empty rather than throwing.

	QFile file ( store_path () );
	QVERIFY ( file.open ( QIODevice::WriteOnly ) );
	file.write ( "{ this is not valid json" );
	file.close ();

	SettingsStore store ( store_path () );

	QCOMPARE ( store.value_string ( QStringLiteral ( "k" ), QStringLiteral ( "default" ) ), QStringLiteral ( "default" ) );

	// It can still write cleanly afterwards.

	QVERIFY ( store.set_int ( QStringLiteral ( "k" ), 5 ) );
	QCOMPARE ( store.value_int ( QStringLiteral ( "k" ), 0 ), 5 );
}

//---------------------------------------------------------------------------------------------------------------------
// The on-disk shape
//---------------------------------------------------------------------------------------------------------------------

// Asserted on the BYTES the store wrote, not on a formatter helper: the claim is about what lands in the user's
// settings file, and a helper tested in isolation can be perfectly correct while save() calls something else. This is
// also the only place the two halves meet -- Qt writes the leaf tokens, this file writes the structure around them.

void TestSettingsStore::writes_allman_braced_json ()
{
	SettingsStore store ( store_path () );

	store.set_bool        ( QStringLiteral ( "general.checkForUpdatesAutomatically" ), true );
	store.set_string      ( QStringLiteral ( "general.theme" ), QStringLiteral ( "Dark" ) );
	store.set_string_list ( QStringLiteral ( "toolbar.layout" ), { QStringLiteral ( "new" ), QStringLiteral ( "open" ) } );
	store.set_string_list ( QStringLiteral ( "files.recent" ), QStringList () );

	QVERIFY ( store.save () );

	QFile file ( store_path () );

	QVERIFY ( file.open ( QIODevice::ReadOnly | QIODevice::Text ) );

	const QString written = QString::fromUtf8 ( file.readAll () );

	// An array opens on its OWN line, at the key's indent -- the whole point of the change. Qt's own indented output
	// puts the bracket on the key's line, so this fails against QJsonDocument::Indented.

	QVERIFY2
	(
		written.contains ( QStringLiteral ( "\"toolbar.layout\":\n    [\n" ) ),
		qPrintable ( written )
	);

	// A scalar stays on its key's line, so the change reaches containers and nothing else.

	QVERIFY2
	(
		written.contains ( QStringLiteral ( "\"general.theme\": \"Dark\"" ) ),
		qPrintable ( written )
	);

	// No key OPENS A BLOCK on its own line -- the K&R form this replaced, asserted as an absence so a single missed
	// container cannot hide among the ones that are right.
	//
	// The trailing newline in the needle is load-bearing rather than incidental: without it this also matches the
	// legitimate inline empty container, "files.recent": [], and the case fails against correct output. What is being
	// excluded is a bracket that opens a block on the key's line, not a bracket on the key's line.

	QVERIFY2 ( !written.contains ( QStringLiteral ( "\": [\n" ) ), qPrintable ( written ) );
	QVERIFY2 ( !written.contains ( QStringLiteral ( "\": {\n" ) ), qPrintable ( written ) );

	// An EMPTY container stays inline: two lines to say "nothing here" is noise rather than structure.

	QVERIFY2 ( written.contains ( QStringLiteral ( "\"files.recent\": []" ) ), qPrintable ( written ) );
}

// The formatting change must be invisible to every reader -- of this build and of any other. Content is what is
// asserted here, because whitespace between tokens is the only thing that moved.

void TestSettingsStore::allman_output_still_round_trips ()
{
	{
		SettingsStore writer ( store_path () );

		// The values that punish a hand-rolled escaper, which is why leaf_token borrows Qt's: a Windows path full of
		// backslashes, a quotation mark, a non-ASCII character, and binary carried as base64.

		writer.set_string ( QStringLiteral ( "system.logFolder" ), QStringLiteral ( "C:\\Users\\Rohin\\VJE Logs" ) );
		writer.set_string ( QStringLiteral ( "import.xmlTextValueKey" ), QStringLiteral ( "say \"hello\" / caf\u00e9" ) );
		writer.set_int    ( QStringLiteral ( "codeView.indentSize" ), 2 );
		writer.set_bool   ( QStringLiteral ( "formView.wrapStrings" ), false );
		writer.set_bytes  ( QStringLiteral ( "window.geometry" ), QByteArray ( "\x01\x02\x03\xff", 4 ) );
		writer.set_string_list ( QStringLiteral ( "toolbar.layout" ), { QStringLiteral ( "new" ), QStringLiteral ( "separator" ) } );

		QVERIFY ( writer.save () );
	}

	SettingsStore reader ( store_path () );

	QCOMPARE ( reader.value_string ( QStringLiteral ( "system.logFolder" ), QString () ),
	           QStringLiteral ( "C:\\Users\\Rohin\\VJE Logs" ) );

	QCOMPARE ( reader.value_string ( QStringLiteral ( "import.xmlTextValueKey" ), QString () ),
	           QStringLiteral ( "say \"hello\" / caf\u00e9" ) );

	QCOMPARE ( reader.value_int  ( QStringLiteral ( "codeView.indentSize" ), 0 ), 2 );
	QCOMPARE ( reader.value_bool ( QStringLiteral ( "formView.wrapStrings" ), true ), false );
	QCOMPARE ( reader.value_bytes ( QStringLiteral ( "window.geometry" ) ), QByteArray ( "\x01\x02\x03\xff", 4 ) );
	QCOMPARE ( reader.value_string_list ( QStringLiteral ( "toolbar.layout" ) ),
	           QStringList ( { QStringLiteral ( "new" ), QStringLiteral ( "separator" ) } ) );

	// And it is still JSON by an independent reader, not merely by ours.

	QFile file ( store_path () );

	QVERIFY ( file.open ( QIODevice::ReadOnly ) );

	QJsonParseError error {};

	const QJsonDocument document = QJsonDocument::fromJson ( file.readAll (), &error );

	QCOMPARE ( error.error, QJsonParseError::NoError );
	QVERIFY  ( document.isObject () );
}

QTEST_MAIN ( TestSettingsStore )
#include "tst_settings_store.moc"

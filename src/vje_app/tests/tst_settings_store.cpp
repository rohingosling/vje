//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

#include "services/SettingsStore.hpp"

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

QTEST_MAIN ( TestSettingsStore )
#include "tst_settings_store.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonDocument: initial state, root replacement with the reset() signal, pointer resolution
//   through the document, and the dirty / file-path change signals (emitted only on an actual change).
//
//   Uses QTEST_GUILESS_MAIN so a QCoreApplication exists for QSignalSpy; no widgets, no display.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>
#include <QSignalSpy>

using namespace vje;

class TestJsonDocument : public QObject
{
	Q_OBJECT

private slots:

	void initial_state_is_empty ();
	void set_root_emits_reset ();
	void resolves_pointers_through_the_document ();
	void dirty_changes_emit_once ();
	void file_path_changes_emit_once ();
};

void TestJsonDocument::initial_state_is_empty ()
{
	JsonDocument document;

	QVERIFY  ( !document.has_root () );
	QVERIFY  ( document.root () == nullptr );
	QVERIFY  ( !document.is_dirty () );
	QCOMPARE ( document.file_path (), QString () );
}

void TestJsonDocument::set_root_emits_reset ()
{
	JsonDocument document;
	QSignalSpy resetSpy ( &document, &JsonDocument::reset );

	document.set_root ( JsonNode::make_object () );

	QCOMPARE ( resetSpy.count (), 1 );
	QVERIFY  ( document.has_root () );
	QCOMPARE ( document.root ()->kind (), JsonKind::Object );
}

void TestJsonDocument::resolves_pointers_through_the_document ()
{
	JsonDocument document;

	ParseResult result = JsonParser::parse ( R"({"projects":[{"name":"vje"}]})" );
	QVERIFY ( result.ok );
	document.set_root ( std::move ( result.root ) );

	JsonNode* name = document.resolve ( JsonPointer::parse ( "/projects/0/name" ) );

	QVERIFY  ( name != nullptr );
	QCOMPARE ( name->string_value (), QStringLiteral ( "vje" ) );

	// An empty document resolves nothing.

	JsonDocument empty;
	QVERIFY ( empty.resolve ( JsonPointer::parse ( "/anything" ) ) == nullptr );
}

void TestJsonDocument::dirty_changes_emit_once ()
{
	JsonDocument document;
	QSignalSpy dirtySpy ( &document, &JsonDocument::dirty_changed );

	document.set_dirty ( true );
	document.set_dirty ( true );                             // No change: no second signal.

	QCOMPARE ( dirtySpy.count (), 1 );
	QCOMPARE ( dirtySpy.at ( 0 ).at ( 0 ).toBool (), true );
	QVERIFY  ( document.is_dirty () );

	document.set_dirty ( false );
	QCOMPARE ( dirtySpy.count (), 2 );
}

void TestJsonDocument::file_path_changes_emit_once ()
{
	JsonDocument document;
	QSignalSpy pathSpy ( &document, &JsonDocument::file_path_changed );

	document.set_file_path ( "/tmp/a.json" );
	document.set_file_path ( "/tmp/a.json" );               // No change: no second signal.

	QCOMPARE ( pathSpy.count (), 1 );
	QCOMPARE ( document.file_path (), QStringLiteral ( "/tmp/a.json" ) );
}

QTEST_GUILESS_MAIN ( TestJsonDocument )

#include "tst_json_document.moc"

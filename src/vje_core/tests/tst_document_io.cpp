//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for DocumentIo: load parses and reports a line/column position on malformed input (FILE-06) while
//   preserving pre-existing duplicate keys; save_bytes produces UTF-8 with NO BOM, LF endings, and a single trailing
//   newline (FILE-03); a file round-trip through a temporary directory reloads to an equal model; new_document is a
//   fresh empty object.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/DocumentIo.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>

using namespace vje;

class TestDocumentIo : public QObject
{
	Q_OBJECT

private slots:

	void load_ok ();
	void load_reports_error_position ();
	void load_preserves_duplicate_keys ();
	void save_bytes_utf8_lf_single_trailing_newline ();
	void save_bytes_has_no_bom ();
	void file_round_trip ();
	void save_file_reports_failure ();
	void new_document_is_empty_object ();
};

void TestDocumentIo::load_ok ()
{
	LoadResult result = DocumentIo::load_text ( QStringLiteral ( "{\"a\":1}" ) );

	QVERIFY ( result.ok );
	QVERIFY ( result.root != nullptr );
	QCOMPARE ( result.root->kind (), JsonKind::Object );
	QVERIFY ( result.duplicateKeys.empty () );
}

void TestDocumentIo::load_reports_error_position ()
{
	LoadResult result = DocumentIo::load_text ( QStringLiteral ( "{\"a\": }" ) );

	QVERIFY ( !result.ok );
	QVERIFY ( result.root == nullptr );
	QVERIFY ( !result.error.message.isEmpty () );
	QCOMPARE ( result.error.line, 1 );
	QVERIFY ( result.error.column > 0 );
}

void TestDocumentIo::load_preserves_duplicate_keys ()
{
	LoadResult result = DocumentIo::load_text ( QStringLiteral ( "{\"a\":1,\"a\":2}" ) );

	QVERIFY ( result.ok );                                 // Duplicates are accepted on load (VAL-02).
	QCOMPARE ( result.root->member_count (), 2 );
	QCOMPARE ( static_cast<int> ( result.duplicateKeys.size () ), 1 );
	QCOMPARE ( result.duplicateKeys.front ().key, QStringLiteral ( "a" ) );
}

void TestDocumentIo::save_bytes_utf8_lf_single_trailing_newline ()
{
	std::unique_ptr<JsonNode> root = JsonNode::make_object ();
	root->append_member ( QStringLiteral ( "a" ), JsonNode::make_number ( QStringLiteral ( "1" ) ) );

	FormatProfile profile;
	profile.braceStyle = BraceStyle::KAndR;

	const QByteArray bytes = DocumentIo::save_bytes ( *root, profile );

	QCOMPARE ( bytes, QByteArray ( "{\n  \"a\": 1\n}\n" ) );

	// Exactly one trailing newline, and LF (no CR).

	QVERIFY ( bytes.endsWith ( '\n' ) );
	QVERIFY ( !bytes.endsWith ( "\n\n" ) );
	QVERIFY ( !bytes.contains ( '\r' ) );
}

void TestDocumentIo::save_bytes_has_no_bom ()
{
	// A non-ASCII key would still never be prefixed with a UTF-8 BOM (EF BB BF).

	std::unique_ptr<JsonNode> root = JsonNode::make_object ();
	root->append_member ( QString::fromUtf8 ( "café" ), JsonNode::make_boolean ( true ) );

	const QByteArray bytes = DocumentIo::save_bytes ( *root, FormatProfile () );

	QVERIFY ( !bytes.startsWith ( QByteArray::fromHex ( "EFBBBF" ) ) );
	QVERIFY ( bytes.startsWith ( '{' ) );
}

void TestDocumentIo::file_round_trip ()
{
	QTemporaryDir dir;
	QVERIFY ( dir.isValid () );

	const QString path = QDir ( dir.path () ).filePath ( QStringLiteral ( "doc.json" ) );

	std::unique_ptr<JsonNode> root = JsonNode::make_object ();
	root->append_member ( QStringLiteral ( "id" ), JsonNode::make_number ( QStringLiteral ( "1.0" ) ) );
	root->append_member ( QStringLiteral ( "name" ), JsonNode::make_string ( QStringLiteral ( "Zero" ) ) );

	QString error;
	QVERIFY ( DocumentIo::save_file ( path, *root, FormatProfile (), &error ) );
	QVERIFY ( error.isEmpty () );

	LoadResult loaded = DocumentIo::load_file ( path );

	QVERIFY ( loaded.ok );
	QVERIFY ( root->equals ( *loaded.root ) );             // Member order + the 1.0 number token survive (FILE-10).
}

void TestDocumentIo::save_file_reports_failure ()
{
	// A path inside a non-existent directory cannot be opened for writing.

	const QString path = QStringLiteral ( "no_such_dir_vje/never/doc.json" );

	std::unique_ptr<JsonNode> root = JsonNode::make_object ();

	QString error;
	QVERIFY ( !DocumentIo::save_file ( path, *root, FormatProfile (), &error ) );
	QVERIFY ( !error.isEmpty () );
}

void TestDocumentIo::new_document_is_empty_object ()
{
	std::unique_ptr<JsonNode> root = DocumentIo::new_document ();

	QCOMPARE ( root->kind (), JsonKind::Object );
	QCOMPARE ( root->member_count (), 0 );
}

QTEST_APPLESS_MAIN ( TestDocumentIo )

#include "tst_document_io.moc"

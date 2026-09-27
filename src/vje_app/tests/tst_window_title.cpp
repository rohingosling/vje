//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for window_title (spec section 2.2) -- the title bar's format, headlessly.
//
//   IT EXISTS BECAUSE THERE IS NO MainWindow HARNESS. The format is a stated rule with three cases and two literal
//   characters in it, and pulling the decision out of update_title is what makes any of them assertable at all.
//
//   THE CHARACTERS ARE ASSERTED BY CODE POINT rather than by pasting the glyphs into a comparison, so a file saved in
//   the wrong encoding fails here rather than shipping a title bar full of question marks.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "window_title.hpp"

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	const QChar SEPARATOR_CHARACTER = QChar ( 0x25AA );   // BLACK SMALL SQUARE
	const QChar MODIFIED_CHARACTER  = QChar ( 0x25CF );   // BLACK CIRCLE
}

class TestWindowTitle : public QObject
{
	Q_OBJECT

private slots:

	void the_application_comes_first ();
	void an_unmodified_document_carries_no_marker ();
	void a_modified_document_carries_the_marker_after_the_file_name ();
	void no_document_is_the_application_name_alone ();
	void an_untitled_document_is_still_a_document ();
	void the_punctuation_is_the_characters_the_spec_names ();
};

void TestWindowTitle::the_application_comes_first ()
{
	const QString title = window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "test-file.json" ), false );

	// The whole point of the 2026-08-15 revision: a taskbar button, an Alt+Tab entry and a window list all truncate
	// from the RIGHT, so the constant goes where truncation cannot reach it.

	QVERIFY  ( title.startsWith ( QStringLiteral ( "VJE" ) ) );
	QVERIFY  ( title.endsWith ( QStringLiteral ( "test-file.json" ) ) );
	QVERIFY2 ( title.indexOf ( QStringLiteral ( "VJE" ) ) < title.indexOf ( QStringLiteral ( "test-file.json" ) ),
	           qPrintable ( title ) );
}

void TestWindowTitle::an_unmodified_document_carries_no_marker ()
{
	const QString title = window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "test-file.json" ), false );

	QCOMPARE ( title, QStringLiteral ( "VJE " ) + SEPARATOR_CHARACTER + QStringLiteral ( " test-file.json" ) );

	QVERIFY ( !title.contains ( MODIFIED_CHARACTER ) );
}

void TestWindowTitle::a_modified_document_carries_the_marker_after_the_file_name ()
{
	const QString title = window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "test-file.json" ), true );

	QCOMPARE ( title,
	           QStringLiteral ( "VJE " ) + SEPARATOR_CHARACTER + QStringLiteral ( " test-file.json " ) + MODIFIED_CHARACTER );

	// AFTER the file name, which is the thing that has unsaved changes -- not after the application, which does not.

	QVERIFY ( title.indexOf ( QStringLiteral ( "test-file.json" ) ) < title.indexOf ( MODIFIED_CHARACTER ) );
}

void TestWindowTitle::no_document_is_the_application_name_alone ()
{
	// Nothing open: no separator and no marker. They go together -- a window with no document has nothing that could
	// be modified, so a marker here would name a document that is not there.

	QCOMPARE ( window_title ( QStringLiteral ( "VJE" ), QString (), false ), QStringLiteral ( "VJE" ) );
	QCOMPARE ( window_title ( QStringLiteral ( "VJE" ), QString (), true  ), QStringLiteral ( "VJE" ) );
}

void TestWindowTitle::an_untitled_document_is_still_a_document ()
{
	// "VJE" alone means nothing is OPEN rather than nothing is SAVED. A new document has no file yet and takes the
	// name its caller supplies, so it keeps its slot in the title -- and its marker, since it is dirty from the moment
	// anything is typed into it.

	QCOMPARE ( window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "Untitled" ), false ),
	           QStringLiteral ( "VJE " ) + SEPARATOR_CHARACTER + QStringLiteral ( " Untitled" ) );

	QCOMPARE ( window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "Untitled" ), true ),
	           QStringLiteral ( "VJE " ) + SEPARATOR_CHARACTER + QStringLiteral ( " Untitled " ) + MODIFIED_CHARACTER );
}

void TestWindowTitle::the_punctuation_is_the_characters_the_spec_names ()
{
	// By CODE POINT, not by pasting the glyphs: a source file saved in the wrong encoding would otherwise ship a title
	// bar full of question marks and pass a comparison against its own mangled literal.

	QCOMPARE ( QString::fromUtf8 ( config::window::TITLE_SEPARATOR ),
	           QStringLiteral ( " " ) + SEPARATOR_CHARACTER + QStringLiteral ( " " ) );

	QCOMPARE ( QString::fromUtf8 ( config::window::TITLE_MODIFIED_MARKER ),
	           QStringLiteral ( " " ) + MODIFIED_CHARACTER );

	// And neither is the asterisk this replaced, nor Qt's placeholder -- which is what update_title would have to use
	// if it called setWindowModified.

	const QString title = window_title ( QStringLiteral ( "VJE" ), QStringLiteral ( "a.json" ), true );

	QVERIFY ( !title.contains ( QLatin1Char ( '*' ) ) );
	QVERIFY ( !title.contains ( QStringLiteral ( "[*]" ) ) );
}

QTEST_APPLESS_MAIN ( TestWindowTitle )

#include "tst_window_title.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for PrintController -- File > Page Setup... and File > Print... (FILE-12).
//
//   WHAT MAKES THIS TESTABLE AT ALL is the same seam the file lifecycle uses: QPrintDialog::exec() blocks on a native
//   modal, so it is behind IDialogService and a fake answers it. What is left either side of that answer is everything
//   worth asserting -- whether there was anything to print, whether a refusal reached both channels, whether the
//   printer Page Setup configured is the one Print then used, and how many pages came out.
//
//   THE JOB IS REAL. The session printer is pointed at a PDF file and the whole pipeline runs against it, so the page
//   count is counted rather than estimated and the bytes are on disk at the end. That is the one part of printing no
//   amount of pure logic can stand in for.
//
//   It needs the GUI harness for the fonts and the paint device, and Qt PrintSupport for the printer; it needs no
//   display, and it asserts nothing about keyboard focus (lessons-learned Q10).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/PrintController.hpp"

#include "printing/page_furniture.hpp"
#include "printing/print_html.hpp"
#include "services/IDialogService.hpp"
#include "services/settings_profiles.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPageLayout>
#include <QPrinter>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <memory>

using namespace vje;

namespace
{
	//-----------------------------------------------------------------------------------------------------------------
	// The scripted dialog service. Only the two print modals and the error box matter here; the rest are present
	// because the seam requires them and refuse, so a stray call would fail a case rather than pass one quietly.
	//-----------------------------------------------------------------------------------------------------------------

	class FakePrintDialogs : public IDialogService
	{
	public:

		QString choose_file_to_open ( const QString&, const QString&, const QString& ) override { return QString (); }
		QString choose_file_to_save ( const QString&, const QString&, const QString& ) override { return QString (); }
		QString choose_folder       ( const QString&, const QString& ) override                 { return QString (); }

		SaveChangesAnswer ask_save_changes ( const QString& ) override { return SaveChangesAnswer::Cancel; }

		void show_error ( const QString&, const QString& message ) override
		{
			++errorCount;

			lastErrorMessage = message;
		}

		void show_information ( const QString&, const QString& ) override {}

		bool confirm ( const QString&, const QString& ) override { return false; }

		bool run_xml_import_dialog ( XmlImportController&, const QString& ) override { return false; }

		bool run_page_setup_dialog ( QPrinter& printer ) override
		{
			++pageSetupCount;

			printerSeenByPageSetup = &printer;

			if ( pageSetupAccepts )
			{
				// Standing in for the user choosing a paper orientation. What the case then checks is that the choice
				// is still on the printer the next Print runs against.

				printer.setPageOrientation ( QPageLayout::Landscape );
			}

			return pageSetupAccepts;
		}

		bool run_print_dialog ( QPrinter& printer ) override
		{
			++printDialogCount;

			printerSeenByPrint = &printer;

			return printDialogAccepts;
		}

		// -- Scripted answers.

		bool pageSetupAccepts   = false;
		bool printDialogAccepts = false;

		// -- Recorded traffic.

		int pageSetupCount   = 0;
		int printDialogCount = 0;
		int errorCount       = 0;

		QString lastErrorMessage;

		QPrinter* printerSeenByPageSetup = nullptr;
		QPrinter* printerSeenByPrint     = nullptr;
	};
}

class TestPrintController : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void nothing_to_print_is_refused_in_both_channels_and_opens_no_dialog ();
	void a_cancelled_print_dialog_prints_nothing_and_says_nothing ();
	void a_print_reaches_the_printer_and_reports_its_page_count ();
	void a_long_rendering_is_paginated ();
	void page_setup_configures_the_printer_the_next_print_uses ();
	void a_cancelled_page_setup_changes_nothing_and_reports_nothing ();
	void the_page_header_names_the_document_and_nothing_else ();
	void the_page_header_names_untitled_before_the_document_has_a_path ();
	void the_page_rules_are_drawn_by_default ();
	void the_page_rules_can_be_turned_off ();
	void an_over_wide_preformatted_line_wraps_within_the_page ();
	void the_view_is_asked_for_its_rendering_at_the_page_s_width ();

private:

	// Point the session printer at a PDF inside the case's own temporary directory, and answer its path.

	QString print_to_pdf_file ( const QString& fileName );

	static PrintContent sample_table ();
	static PrintContent preformatted ( const QString& text );

	// The first /MediaBox in a produced PDF -- the page's own statement of its size, in points. Read from the bytes
	// because it is the only witness to what the JOB used, as distinct from what the printer OBJECT was set to.

	static bool first_media_box ( const QString& path, QSizeF* outSize );

	// How many rows of a rendered page are inked ACROSS THEIR WHOLE WIDTH -- which is what a hairline is and what no
	// amount of text can be.

	static int full_width_rules ( const QImage& page );

	// Paint one page's furniture onto a white image, so a paint claim is checked by reading pixels (lessons Q12).

	static QImage rendered_furniture ( bool rules );

	std::unique_ptr<QTemporaryDir>   directory;
	std::unique_ptr<QTemporaryDir>   settingsDirectory;
	std::unique_ptr<SettingsStore>   settings;
	std::unique_ptr<JsonDocument>    document;
	std::unique_ptr<StatusService>   status;
	std::unique_ptr<FakePrintDialogs> dialogs;
	std::unique_ptr<PrintController> controller;

	QStringList statusMessages;
};

//=====================================================================================================================
// Fixture
//=====================================================================================================================

void TestPrintController::init ()
{
	directory = std::make_unique<QTemporaryDir> ();

	QVERIFY ( directory->isValid () );

	settingsDirectory = std::make_unique<QTemporaryDir> ();

	QVERIFY ( settingsDirectory->isValid () );

	settings = std::make_unique<SettingsStore>
	(
		settingsDirectory->filePath ( QStringLiteral ( "settings.json" ) )
	);

	document = std::make_unique<JsonDocument> ();
	status   = std::make_unique<StatusService> ();
	dialogs  = std::make_unique<FakePrintDialogs> ();

	statusMessages.clear ();

	connect ( status.get (), &StatusService::message_posted, this, [ this ] ( const QString& text, int )
	{
		statusMessages.append ( text );
	} );

	controller = std::make_unique<PrintController> ( document.get (), settings.get (), dialogs.get (), status.get () );
}

void TestPrintController::cleanup ()
{
	// Reverse construction order: a QObject destructor is not passive, and a collaborator outliving the thing that
	// points at it is how this project has met that trap before (lessons-learned Q1).

	controller.reset ();
	dialogs.reset ();
	status.reset ();
	document.reset ();
	settings.reset ();
	settingsDirectory.reset ();
	directory.reset ();
}

QString TestPrintController::print_to_pdf_file ( const QString& fileName )
{
	const QString path = directory->filePath ( fileName );

	controller->printer ().setOutputFormat ( QPrinter::PdfFormat );
	controller->printer ().setOutputFileName ( path );

	return path;
}

PrintContent TestPrintController::sample_table ()
{
	PrintContent content;

	content.kind     = PrintContent::Kind::Table;
	content.viewName = QStringLiteral ( "Form View" );
	content.subject  = QStringLiteral ( "/people" );
	content.headers  = { QStringLiteral ( "name" ), QStringLiteral ( "age" ) };
	content.rows     = { { QStringLiteral ( "Ada" ), QStringLiteral ( "36" ) },
	                     { QStringLiteral ( "Alan" ), QStringLiteral ( "41" ) } };

	return content;
}

bool TestPrintController::first_media_box ( const QString& path, QSizeF* outSize )
{
	QFile file ( path );

	if ( !file.open ( QIODevice::ReadOnly ) )
	{
		return false;
	}

	const QString bytes = QString::fromLatin1 ( file.readAll () );

	const QRegularExpression pattern
	(
		QStringLiteral ( "/MediaBox\\s*\\[\\s*[-\\d.]+\\s+[-\\d.]+\\s+([\\d.]+)\\s+([\\d.]+)\\s*\\]" )
	);

	const QRegularExpressionMatch match = pattern.match ( bytes );

	if ( !match.hasMatch () )
	{
		return false;
	}

	*outSize = QSizeF ( match.captured ( 1 ).toDouble (), match.captured ( 2 ).toDouble () );

	return true;
}

int TestPrintController::full_width_rules ( const QImage& page )
{
	// A hairline is the one thing on a printed page that is inked from edge to edge. Text never is, however dense, so
	// counting rows that are inked at BOTH margins and most of the way between distinguishes the two without knowing
	// where either was drawn.

	int rules = 0;

	for ( int y = 0; y < page.height (); ++y )
	{
		if ( ( qGray ( page.pixel ( 0, y ) ) >= 128 ) || ( qGray ( page.pixel ( page.width () - 1, y ) ) >= 128 ) )
		{
			continue;
		}

		int inked = 0;

		for ( int x = 0; x < page.width (); ++x )
		{
			if ( qGray ( page.pixel ( x, y ) ) < 128 )
			{
				++inked;
			}
		}

		if ( inked >= ( ( page.width () * 9 ) / 10 ) )
		{
			++rules;
		}
	}

	return rules;
}

QImage TestPrintController::rendered_furniture ( bool rules )
{
	QImage page ( 600, 400, QImage::Format_RGB32 );

	page.fill ( Qt::white );

	QFont furnitureFont;

	furnitureFont.setPointSize ( 8 );

	PageFurniture furniture;

	furniture.title     = QStringLiteral ( "smoke-test-1.json" );
	furniture.pageIndex = 1;
	furniture.pageCount = 7;
	furniture.rules     = rules;

	QPainter painter ( &page );

	paint_page_furniture ( painter, furnitureFont, QSizeF ( page.size () ), furniture );

	painter.end ();

	return page;
}

PrintContent TestPrintController::preformatted ( const QString& text )
{
	PrintContent content;

	content.kind     = PrintContent::Kind::Preformatted;
	content.viewName = QStringLiteral ( "Text View" );
	content.text     = text;

	return content;
}

//=====================================================================================================================
// Cases
//=====================================================================================================================

void TestPrintController::nothing_to_print_is_refused_in_both_channels_and_opens_no_dialog ()
{
	// The content source is deliberately left unset, which is the state a window with no editor pane is in. The
	// refusal is the one print outcome a user cannot see for themselves -- the paper never appears -- so it goes to
	// both the modal and the status bar, exactly as an export refusal does (Phase 12.5).

	dialogs->printDialogAccepts = true;

	QVERIFY ( !controller->print () );

	QCOMPARE ( dialogs->errorCount, 1 );
	QCOMPARE ( statusMessages.size (), 1 );

	// And the print dialog is never reached: asking the user to choose a printer for a job that was never going to
	// run is a question with no useful answer.

	QCOMPARE ( dialogs->printDialogCount, 0 );

	QCOMPARE ( controller->pages_printed (), 0 );
}

void TestPrintController::a_cancelled_print_dialog_prints_nothing_and_says_nothing ()
{
	controller->set_content_source ( [] ( int ) { return sample_table (); } );

	dialogs->printDialogAccepts = false;

	const QString path = print_to_pdf_file ( QStringLiteral ( "cancelled.pdf" ) );

	QVERIFY ( !controller->print () );

	QCOMPARE ( dialogs->printDialogCount, 1 );

	// A cancel is neither a success nor a refusal. The user knows what they just did, so neither channel says
	// anything -- and nothing reaches the printer.

	QCOMPARE ( dialogs->errorCount, 0 );
	QVERIFY2 ( statusMessages.isEmpty (), qPrintable ( statusMessages.join ( QLatin1Char ( '|' ) ) ) );

	QVERIFY ( !QFileInfo::exists ( path ) );
}

void TestPrintController::a_print_reaches_the_printer_and_reports_its_page_count ()
{
	controller->set_content_source ( [] ( int ) { return sample_table (); } );

	dialogs->printDialogAccepts = true;

	const QString path = print_to_pdf_file ( QStringLiteral ( "table.pdf" ) );

	QVERIFY ( controller->print () );

	// The job is real: a PDF with bytes in it.

	const QFileInfo produced ( path );

	QVERIFY2 ( produced.exists (), qPrintable ( path ) );
	QVERIFY2 ( produced.size () > 0, qPrintable ( QStringLiteral ( "%1 bytes" ).arg ( produced.size () ) ) );

	QCOMPARE ( controller->pages_printed (), 1 );

	// The success reports in the status bar alone -- a modal on a completed command reads as a failure (Phase 12.5).

	QCOMPARE ( dialogs->errorCount, 0 );
	QCOMPARE ( statusMessages.size (), 1 );

	QVERIFY2 ( statusMessages.first ().contains ( QStringLiteral ( "1 page" ) ),
	           qPrintable ( statusMessages.first () ) );
}

void TestPrintController::a_long_rendering_is_paginated ()
{
	// The pagination is ours rather than QTextDocument::print's, which is what makes the page count a fact about the
	// job. A rendering several hundred lines long must come out as several pages, not one clipped sheet.

	QStringList lines;

	for ( int line = 0; line < 500; ++line )
	{
		lines.append ( QStringLiteral ( "  \"key%1\": \"value %1\"," ).arg ( line ) );
	}

	const QString text = lines.join ( QLatin1Char ( '\n' ) );

	controller->set_content_source ( [ text ] ( int ) { return preformatted ( text ); } );

	dialogs->printDialogAccepts = true;

	print_to_pdf_file ( QStringLiteral ( "long.pdf" ) );

	QVERIFY ( controller->print () );

	QVERIFY2 ( controller->pages_printed () > 1,
	           qPrintable ( QStringLiteral ( "500 lines came out as %1 page(s)" ).arg ( controller->pages_printed () ) ) );

	QVERIFY2 ( statusMessages.first ().contains ( QStringLiteral ( "pages" ) ),
	           qPrintable ( statusMessages.first () ) );
}

void TestPrintController::page_setup_configures_the_printer_the_next_print_uses ()
{
	// The whole reason Page Setup is a separate command: the paper a user chooses has to still be there when they
	// press Ctrl+P. One printer for the session is what makes that true, and this is the case that says so.

	dialogs->pageSetupAccepts = true;

	QVERIFY ( controller->page_setup () );

	QCOMPARE ( controller->printer ().pageLayout ().orientation (), QPageLayout::Landscape );

	controller->set_content_source ( [] ( int ) { return sample_table (); } );

	dialogs->printDialogAccepts = true;

	const QString path = print_to_pdf_file ( QStringLiteral ( "landscape.pdf" ) );

	QVERIFY ( controller->print () );

	// Same object, both times -- not a copy, and not a fresh printer built for the job.

	QVERIFY ( dialogs->printerSeenByPageSetup != nullptr );
	QCOMPARE ( dialogs->printerSeenByPrint, dialogs->printerSeenByPageSetup );

	QCOMPARE ( controller->printer ().pageLayout ().orientation (), QPageLayout::Landscape );

	// And the PAPER says so. Asking the printer object is not enough: it answers about the object the dialog wrote to,
	// which a job rendering onto some other printer would leave looking perfectly correct. The page box in the
	// produced PDF is the only witness that the choice reached the output.

	QSizeF mediaBox;

	QVERIFY2 ( first_media_box ( path, &mediaBox ), qPrintable ( path ) );

	QVERIFY2 ( mediaBox.width () > mediaBox.height (),
	           qPrintable ( QStringLiteral ( "page box is %1 x %2" ).arg ( mediaBox.width () ).arg ( mediaBox.height () ) ) );
}

void TestPrintController::a_cancelled_page_setup_changes_nothing_and_reports_nothing ()
{
	dialogs->pageSetupAccepts = false;

	QVERIFY ( !controller->page_setup () );

	QCOMPARE ( dialogs->pageSetupCount, 1 );
	QVERIFY  ( statusMessages.isEmpty () );

	QCOMPARE ( controller->printer ().pageLayout ().orientation (), QPageLayout::Portrait );
}

void TestPrintController::the_page_header_names_the_document_and_nothing_else ()
{
	document->set_root ( JsonNode::make_object () );
	document->set_file_path ( directory->filePath ( QStringLiteral ( "people.json" ) ) );

	const QString header = controller->header_text ();

	// Named the way the title bar and the status bar name it -- the same function, so the three cannot drift.

	QCOMPARE ( header, QStringLiteral ( "people.json" ) );

	// The head carried the selected node's pointer and the view's name too, and now carries neither. Nothing about the
	// CONTENT can put anything there: the header does not take one, which is what makes that structural rather than a
	// promise kept by the one call site.

	QVERIFY2 ( !header.contains ( QStringLiteral ( "/" ) ),    qPrintable ( header ) );
	QVERIFY2 ( !header.contains ( QStringLiteral ( "View" ) ), qPrintable ( header ) );
}

void TestPrintController::the_page_header_names_untitled_before_the_document_has_a_path ()
{
	document->set_root ( JsonNode::make_object () );

	const QString header = controller->header_text ();

	QCOMPARE ( header.trimmed (), header );

	QVERIFY2 ( header.contains ( QStringLiteral ( "Untitled" ) ), qPrintable ( header ) );
}

void TestPrintController::the_page_rules_are_drawn_by_default ()
{
	// SET-10's default, checked by reading the rendered pixels rather than the setting -- a paint claim is only checked
	// by comparing what was painted (lessons-learned Q12), and a setting nobody can prove is connected to anything is
	// exactly what this avoids.

	QVERIFY ( print_page_rules ( settings.get () ) );

	QCOMPARE ( full_width_rules ( rendered_furniture ( true ) ), 2 );
}

void TestPrintController::the_page_rules_can_be_turned_off ()
{
	settings->set_bool ( settings_keys::PRINT_PAGE_RULES, false );

	QVERIFY ( !print_page_rules ( settings.get () ) );

	// The rules go and nothing else does: the page still carries its head and its foot, so the image is not simply
	// blank -- which is what would make a "no rules" assertion pass against a furniture painter that did nothing.

	const QImage withoutRules = rendered_furniture ( false );

	QCOMPARE ( full_width_rules ( withoutRules ), 0 );

	QVERIFY2 ( withoutRules != QImage ( withoutRules.size (), withoutRules.format () ),
	           "the page came out blank, so this proves nothing about the rules" );

	bool anyInk = false;

	for ( int y = 0; ( y < withoutRules.height () ) && !anyInk; ++y )
	{
		for ( int x = 0; x < withoutRules.width (); ++x )
		{
			if ( qGray ( withoutRules.pixel ( x, y ) ) < 128 )
			{
				anyInk = true;

				break;
			}
		}
	}

	QVERIFY2 ( anyInk, "the header and footer text vanished with the rules" );
}

void TestPrintController::an_over_wide_preformatted_line_wraps_within_the_page ()
{
	// print_html declares white-space:pre-wrap; this is the case that checks Qt's layout actually honours it, which is
	// the property that matters -- a line clipped at the right margin loses text the reader cannot see going.

	const QString line = QString ( 4000, QLatin1Char ( 'x' ) );

	QTextDocument textDocument;

	PrintStyle style;

	style.bodyFamily     = QStringLiteral ( "Arial" );
	style.fixedFamily    = QStringLiteral ( "Courier New" );
	style.bodyPointSize  = 9;
	style.fixedPointSize = 8;

	textDocument.setDocumentMargin ( 0.0 );
	textDocument.setHtml ( print_html ( preformatted ( line ), style ) );
	textDocument.setPageSize ( QSizeF ( 500.0, 700.0 ) );

	// QTextDocument lays out lazily, and an unlaid block reports ZERO lines -- which would pass a "did not wrap"
	// reading of this case and fail this one for the wrong reason. Asking for the page count forces the layout.

	QVERIFY ( textDocument.pageCount () >= 1 );

	const QTextBlock block = textDocument.firstBlock ();

	QVERIFY ( block.isValid () );
	QVERIFY ( block.layout () != nullptr );

	QVERIFY2 ( block.layout ()->lineCount () > 1,
	           qPrintable ( QStringLiteral ( "4000 characters laid out as %1 line(s) in a 500-unit page" )
	                        .arg ( block.layout ()->lineCount () ) ) );
}

void TestPrintController::the_view_is_asked_for_its_rendering_at_the_page_s_width ()
{
	// The seam's half of the wrapping fix. A view laying its content out by counting characters has to be asked for it
	// at the width it will be READ at -- so the controller measures the page in characters of the print's own
	// fixed-width font and passes that down. Anything at or below zero would leave every view unwrapped and hand the
	// page back the job it does badly.

	int columnsSeen = -1;

	controller->set_content_source ( [ &columnsSeen ] ( int columns )
	{
		columnsSeen = columns;

		return sample_table ();
	} );

	dialogs->printDialogAccepts = true;

	print_to_pdf_file ( QStringLiteral ( "columns.pdf" ) );

	QVERIFY ( controller->print () );

	QVERIFY2 ( columnsSeen > 0, qPrintable ( QStringLiteral ( "the view was asked for %1 columns" ).arg ( columnsSeen ) ) );

	// A sanity bound rather than a pinned number, because the answer depends on the paper and on whichever fixed-width
	// family the machine measured as fixed (style/fixed_font): an A4 page at 8 pt is on the order of a hundred
	// characters, and an answer outside this band means the metric, not the arithmetic.

	QVERIFY2 ( ( columnsSeen > 40 ) && ( columnsSeen < 400 ),
	           qPrintable ( QStringLiteral ( "%1 columns across an A4 page" ).arg ( columnsSeen ) ) );
}

QTEST_MAIN ( TestPrintController )

#include "tst_print_controller.moc"

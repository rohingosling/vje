//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for XmlImportDialog (FILE-13, spec section 2.11) -- the WIRING, and only the wiring. What the dialog
//   shows and what it decides are XmlImportController's and are pinned in tst_xml_import_controller; what is left here
//   is the half that needs widgets: which control reaches which controller call, which control answers which state,
//   and that the preview pane carries the text the controller produced.
//
//   What is pinned:
//
//     - The list is built from the controller's choices, in its order, showing each name and description, with the
//       persisted choice preselected.
//     - Selecting a row changes the controller's strategy and the preview follows it.
//     - The Infer scalar types box reaches the controller.
//     - The Text value key field is live for Custom flattened and insensitive (but present) for the other three, and a
//       keystroke in it reaches the controller IMMEDIATELY -- the coalescing timer defers the re-render, never the
//       value, so an Import pressed mid-type uses what was typed.
//     - Unparseable XML leaves the preview empty, the reason on the notes label, and Import disabled.
//
//   Offscreen: real widgets, no display, and nothing here asserts keyboard focus (lessons-learned Q10).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/XmlImportDialog.hpp"

#include "controllers/XmlImportController.hpp"
#include "style/dialog_surface.hpp"
#include "style/tooltip_text.hpp"
#include "views/CodeEditor.hpp"

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QIcon>
#include <QPalette>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QTextDocument>

#include <memory>

using namespace vje;

namespace
{
	const QString SENTENCE_XML = QStringLiteral
	(
		"<sentence name=\"test-sentence\" tag=\"0\">The cat went up the hill.</sentence>"
	);
}

class TestXmlImportDialog : public QObject
{
	Q_OBJECT

private slots:

	void the_list_is_one_line_per_strategy_and_mirrors_the_preselection ();
	void the_description_block_follows_the_selection ();
	void the_description_is_dimmed_against_ordinary_dialog_text ();
	void selecting_a_strategy_reaches_the_controller_and_the_preview ();
	void the_infer_box_reaches_the_controller ();
	void the_text_key_field_is_live_for_custom_flattened_alone ();
	void a_keystroke_in_the_key_field_reaches_the_controller_at_once ();
	void unparseable_xml_disables_import_and_reports_on_the_notes_label ();
	void the_notes_are_bullets_under_their_own_caption ();
	void the_notes_block_is_hidden_when_there_is_nothing_to_say ();
	void a_note_containing_markup_reaches_the_reader_intact ();
	void the_notes_are_dimmed_like_the_description ();
};

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

// Renamed and rewritten on 2026-08-06 from the_list_mirrors_the_controllers_choices_and_preselection, whose row
// assertion was the OPPOSITE of what section 2.11 now says: it required each row to carry the description as a second
// line. Kept as one case rather than split, because the preselection claim never changed.

void TestXmlImportDialog::the_list_is_one_line_per_strategy_and_mirrors_the_preselection ()
{
	ImportOptions stored;

	stored.xmlStrategy = XmlImportStrategyKind::GroupedAttributes;

	XmlImportController controller ( SENTENCE_XML, stored, FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	const std::vector<XmlStrategyChoice>& choices = XmlImportController::strategy_choices ();

	QCOMPARE ( dialog.strategy_list ()->count (), static_cast<int> ( choices.size () ) );

	for ( int row = 0; row < dialog.strategy_list ()->count (); ++row )
	{
		const XmlStrategyChoice& choice = choices [ static_cast<std::size_t> ( row ) ];
		const QString            text   = dialog.strategy_list ()->item ( row )->text ();

		// The name ALONE, exactly -- not "contains", which the two-line form satisfied too.

		QCOMPARE ( text, choice.labelled_name () );

		QVERIFY2 ( !text.contains ( QLatin1Char ( '\n' ) ), qPrintable ( text ) );

		// The description left the row and became the tooltip's alone, so a narrow dialog still answers a hover -- as
		// STYLE-17's formatter lays it out, since a sentence this long would otherwise run the width of the screen.

		QCOMPARE ( dialog.strategy_list ()->item ( row )->toolTip (), tooltip_text ( choice.description ) );
	}

	QCOMPARE ( dialog.strategy_list ()->currentRow (), controller.selected_index () );
	QVERIFY  ( dialog.import_button ()->isEnabled () );
}

// Section 2.11: the description is shown once, for the row that is selected. Read off the CONTROLLER's choice list so
// the case cannot pass by the dialog echoing text it took from the row it was given.

void TestXmlImportDialog::the_description_block_follows_the_selection ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	const std::vector<XmlStrategyChoice>& choices = XmlImportController::strategy_choices ();

	// It says something from the moment the dialog opens, rather than waiting for the first click.

	QCOMPARE
	(
		dialog.description_label ()->text (),
		choices [ static_cast<std::size_t> ( controller.selected_index () ) ].description
	);

	// ...and it follows every row, including back to the one it started on.

	for ( int row = 0; row < dialog.strategy_list ()->count (); ++row )
	{
		dialog.strategy_list ()->setCurrentRow ( row );

		QCOMPARE ( dialog.description_label ()->text (),
		           choices [ static_cast<std::size_t> ( row ) ].description );
	}
}

// STYLE-15's prose rule. The label must be dimmed from the dialog's own text -- and must NOT have reached for
// QPalette::PlaceholderText, whose sub-AA light value is accepted for placeholders alone (spec section 5).

void TestXmlImportDialog::the_description_is_dimmed_against_ordinary_dialog_text ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	const QColor prose = dialog.description_label ()->palette ().color ( QPalette::WindowText );

	QCOMPARE ( prose.rgb (), dialog_dimmed_prose ( dialog.palette () ).rgb () );

	// Quieter than the text beside it, and not the placeholder role.

	const QColor surface = dialog.palette ().color ( QPalette::Window );
	const QColor text    = dialog.palette ().color ( QPalette::WindowText );

	QVERIFY2
	(
		qAbs ( prose.lightness () - surface.lightness () ) < qAbs ( text.lightness () - surface.lightness () ),
		"the description is not dimmed against the dialog's ordinary text"
	);

	QVERIFY ( prose.rgb () != dialog.palette ().color ( QPalette::PlaceholderText ).rgb () );
}

void TestXmlImportDialog::selecting_a_strategy_reaches_the_controller_and_the_preview ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	QVERIFY ( !dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"@name\"" ) ) );

	dialog.strategy_list ()->setCurrentRow
	(
		XmlImportController::index_of_strategy ( XmlImportStrategyKind::BadgerFish )
	);

	QCOMPARE ( static_cast<int> ( controller.strategy () ),
	           static_cast<int> ( XmlImportStrategyKind::BadgerFish ) );

	// The preview pane carries exactly what the controller produced -- the dialog renders it and rewrites nothing.

	QCOMPARE ( dialog.preview_editor ()->toPlainText (), controller.preview ().text );
	QVERIFY  ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"@name\"" ) ) );
}

void TestXmlImportDialog::the_infer_box_reaches_the_controller ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	QVERIFY ( !dialog.infer_scalars_box ()->isChecked () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"tag\": \"0\"" ) ) );

	dialog.infer_scalars_box ()->setChecked ( true );

	QVERIFY ( controller.infer_scalar_types () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"tag\": 0" ) ) );
}

void TestXmlImportDialog::the_text_key_field_is_live_for_custom_flattened_alone ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	// Present but insensitive under the other strategies, rather than hidden: an option that appears and vanishes is
	// one the user cannot discover from the strategy they happen to be on.

	QVERIFY ( dialog.text_value_key_field ()->isVisibleTo ( &dialog ) );
	QVERIFY ( !dialog.text_value_key_field ()->isEnabled () );

	dialog.strategy_list ()->setCurrentRow
	(
		XmlImportController::index_of_strategy ( XmlImportStrategyKind::CustomFlattened )
	);

	QVERIFY ( dialog.text_value_key_field ()->isEnabled () );

	dialog.strategy_list ()->setCurrentRow
	(
		XmlImportController::index_of_strategy ( XmlImportStrategyKind::BadgerFish )
	);

	QVERIFY ( dialog.text_value_key_field ()->isVisibleTo ( &dialog ) );
	QVERIFY ( !dialog.text_value_key_field ()->isEnabled () );
}

void TestXmlImportDialog::a_keystroke_in_the_key_field_reaches_the_controller_at_once ()
{
	ImportOptions stored;

	stored.xmlStrategy = XmlImportStrategyKind::CustomFlattened;

	XmlImportController controller ( SENTENCE_XML, stored, FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	QVERIFY ( dialog.text_value_key_field ()->isEnabled () );

	// Typed, not set: textEdited is the signal the dialog listens to, and setText would not emit it.

	QTest::keyClicks ( dialog.text_value_key_field (), QStringLiteral ( "body" ) );

	// The VALUE is through immediately even though the re-render is still waiting out the coalescing timer. This is
	// the case that matters: Import pressed on the next keystroke must use "body", and options() is what the pipeline
	// reads. (Verified to fail against a dialog that set the key on the timer instead of on the keystroke.)

	QCOMPARE ( controller.text_value_key (), QStringLiteral ( "body" ) );
	QCOMPARE ( controller.options ().xmlTextValueKey, QStringLiteral ( "body" ) );

	// And the preview does catch up once the timer fires.

	QTRY_VERIFY ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"body\"" ) ) );
}

void TestXmlImportDialog::unparseable_xml_disables_import_and_reports_on_the_notes_label ()
{
	XmlImportController controller
	(
		QStringLiteral ( "<root><unclosed></root>" ),
		ImportOptions (),
		FormatProfile ()
	);

	XmlImportDialog dialog ( &controller, QStringLiteral ( "broken.xml" ), QIcon () );

	QVERIFY ( !dialog.import_button ()->isEnabled () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().isEmpty () );

	// Read through a QTextDocument rather than off QLabel::text(): the label is rich text now, so its text() is the
	// markup we built and a raw `contains` would be asserting our own escaping against itself. What has to be true is
	// that the READER sees the controller's sentence, which is what the rendered plain text is.

	QTextDocument rendered;

	rendered.setHtml ( dialog.notes_label ()->text () );

	QVERIFY2 ( rendered.toPlainText ().contains ( controller.preview ().error ),
	           qPrintable ( rendered.toPlainText () ) );
}

// Section 2.11's notes, as one bullet per note under their own caption (2026-08-06). Up to six independent things can
// appear here -- a parse failure, a truncation note and the four degraded-construct warnings -- and run together as
// newline-separated sentences a user with two problems reads one paragraph and sees one.
//
// The expected count is DERIVED from the controller rather than written down, so the case survives a change to which
// constructs this particular file degrades and still fails if a note goes missing or two share a bullet.

void TestXmlImportDialog::the_notes_are_bullets_under_their_own_caption ()
{
	// Namespaced, mixed-content, and an attribute named like a sibling element: three notes from one small file.

	XmlImportController controller
	(
		QStringLiteral ( "<r xmlns:a=\"urn:x\" n=\"1\">text<n>2</n>more<a:b/></r>" ),
		ImportOptions (),
		FormatProfile ()
	);

	XmlImportDialog dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	const XmlImportPreview& preview = controller.preview ();

	const int noteCount = static_cast<int> ( preview.warnings.size () )
	                    + ( preview.error         .isEmpty () ? 0 : 1 )
	                    + ( preview.truncationNote.isEmpty () ? 0 : 1 );

	QVERIFY2 ( noteCount >= 2, "the fixture produced too few notes for the case to mean anything" );

	QCOMPARE ( dialog.notes_label ()->text ().count ( QStringLiteral ( "<li>" ) ), noteCount );

	// The caption is present and shown, so the block is labelled rather than appearing as loose prose.

	QVERIFY ( dialog.notes_caption () != nullptr );
	QVERIFY ( !dialog.notes_caption ()->text ().isEmpty () );
	QVERIFY ( dialog.notes_caption ()->isVisibleTo ( &dialog ) );
}

// The caption goes with its content. A "Notes:" heading over nothing reads as a section that failed to fill rather
// than one with nothing to say -- and on the clean path, which is most of them, that would be every import.

void TestXmlImportDialog::the_notes_block_is_hidden_when_there_is_nothing_to_say ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	QVERIFY2 ( controller.preview ().warnings.isEmpty (), "the fixture degrades something, so the case proves nothing" );
	QVERIFY  ( controller.preview ().error.isEmpty () );

	QVERIFY ( !dialog.notes_caption ()->isVisibleTo ( &dialog ) );
	QVERIFY ( !dialog.notes_label   ()->isVisibleTo ( &dialog ) );
}

// The escaping, and it is LOAD-BEARING rather than defensive. QXmlStreamReader's message for this input is
// "Expected '>' or '/', but got '<'. (line 1, column 9)." -- so the sentence that says what went wrong ends in a '<'
// that rich text reads as the start of a tag, swallowing the rest of the message from the one place the user is
// looking. Verified against a build without toHtmlEscaped: the rendered text stops at "but got ".

void TestXmlImportDialog::a_note_containing_markup_reaches_the_reader_intact ()
{
	XmlImportController controller
	(
		QStringLiteral ( "<root><a<b></root>" ),
		ImportOptions (),
		FormatProfile ()
	);

	XmlImportDialog dialog ( &controller, QStringLiteral ( "broken.xml" ), QIcon () );

	const QString error = controller.preview ().error;

	QVERIFY2 ( error.contains ( QLatin1Char ( '<' ) ),
	           qPrintable ( QStringLiteral ( "the fixture's error carries no markup: %1" ).arg ( error ) ) );

	QTextDocument rendered;

	rendered.setHtml ( dialog.notes_label ()->text () );

	// Contains rather than equals: the note wraps the parser's sentence in "This file could not be read as XML: %1".
	// What is being pinned is that the wrapped sentence survives WHOLE -- unescaped, the rendered text keeps
	// everything up to the '<' and loses the rest, so this is the assertion that fails.

	QVERIFY2 ( rendered.toPlainText ().contains ( error ),
	           qPrintable ( QStringLiteral ( "rendered as: %1" ).arg ( rendered.toPlainText () ) ) );
}

// STYLE-15: the notes are explanatory prose under a caption, exactly as the strategy description is, so they take the
// same tone. Asserted as EQUALITY with the description's rather than against the rule separately -- what would go
// wrong here is the two drifting apart, not either one being wrong on its own.

void TestXmlImportDialog::the_notes_are_dimmed_like_the_description ()
{
	XmlImportController controller
	(
		QStringLiteral ( "<r xmlns:a=\"urn:x\">text<a:b/></r>" ),
		ImportOptions (),
		FormatProfile ()
	);

	XmlImportDialog dialog ( &controller, QStringLiteral ( "sample.xml" ), QIcon () );

	const QColor notes       = dialog.notes_label       ()->palette ().color ( QPalette::WindowText );
	const QColor description = dialog.description_label ()->palette ().color ( QPalette::WindowText );

	QCOMPARE ( notes.rgb (), description.rgb () );
	QCOMPARE ( notes.rgb (), dialog_dimmed_prose ( dialog.palette () ).rgb () );

	// The CAPTION is not dimmed -- it is styled like "Description:", which is ordinary window text. If the tone ever
	// reaches the caption too, the block loses the contrast that makes it read as a heading over prose.

	QCOMPARE ( dialog.notes_caption ()->palette ().color ( QPalette::WindowText ).rgb (),
	           dialog.palette ().color ( QPalette::WindowText ).rgb () );
}

QTEST_MAIN ( TestXmlImportDialog )

#include "tst_xml_import_dialog.moc"

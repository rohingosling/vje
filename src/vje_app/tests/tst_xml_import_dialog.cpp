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
#include "views/CodeEditor.hpp"

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>

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

	void the_list_mirrors_the_controllers_choices_and_preselection ();
	void selecting_a_strategy_reaches_the_controller_and_the_preview ();
	void the_infer_box_reaches_the_controller ();
	void the_text_key_field_is_live_for_custom_flattened_alone ();
	void a_keystroke_in_the_key_field_reaches_the_controller_at_once ();
	void unparseable_xml_disables_import_and_reports_on_the_notes_label ();
};

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestXmlImportDialog::the_list_mirrors_the_controllers_choices_and_preselection ()
{
	ImportOptions stored;

	stored.xmlStrategy = XmlImportStrategyKind::GroupedAttributes;

	XmlImportController controller ( SENTENCE_XML, stored, FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ) );

	const std::vector<XmlStrategyChoice>& choices = XmlImportController::strategy_choices ();

	QCOMPARE ( dialog.strategy_list ()->count (), static_cast<int> ( choices.size () ) );

	for ( int row = 0; row < dialog.strategy_list ()->count (); ++row )
	{
		const XmlStrategyChoice& choice = choices [ static_cast<std::size_t> ( row ) ];
		const QString            text   = dialog.strategy_list ()->item ( row )->text ();

		QVERIFY ( text.contains ( choice.labelled_name () ) );
		QVERIFY ( text.contains ( choice.description ) );      // Section 2.11: name AND one-line description.
	}

	QCOMPARE ( dialog.strategy_list ()->currentRow (), controller.selected_index () );
	QVERIFY  ( dialog.import_button ()->isEnabled () );
}

void TestXmlImportDialog::selecting_a_strategy_reaches_the_controller_and_the_preview ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ) );

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
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ) );

	QVERIFY ( !dialog.infer_scalars_box ()->isChecked () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"tag\": \"0\"" ) ) );

	dialog.infer_scalars_box ()->setChecked ( true );

	QVERIFY ( controller.infer_scalar_types () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().contains ( QStringLiteral ( "\"tag\": 0" ) ) );
}

void TestXmlImportDialog::the_text_key_field_is_live_for_custom_flattened_alone ()
{
	XmlImportController controller ( SENTENCE_XML, ImportOptions (), FormatProfile () );
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ) );

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
	XmlImportDialog     dialog ( &controller, QStringLiteral ( "sample.xml" ) );

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

	XmlImportDialog dialog ( &controller, QStringLiteral ( "broken.xml" ) );

	QVERIFY ( !dialog.import_button ()->isEnabled () );
	QVERIFY ( dialog.preview_editor ()->toPlainText ().isEmpty () );
	QVERIFY ( dialog.notes_label ()->text ().contains ( controller.preview ().error ) );
}

QTEST_MAIN ( TestXmlImportDialog )

#include "tst_xml_import_dialog.moc"

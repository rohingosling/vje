//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonCellDelegate and JsonNumberValidator -- the per-kind editor and the
//   VAL-03 gate shared by the object form and the array table.
//
//   The validator carries the weight here. VAL-03 has to reject "12abc" WITHOUT rejecting "-", "1.", or "1e" -- each of
//   which is nonsense alone but is the halfway state of typing a good number. Getting that wrong in either direction is
//   invisible until someone tries to type a negative or a decimal and finds the field fighting them, so the whole
//   Acceptable / Intermediate / Invalid table is pinned rather than sampled.
//
//   Runs under the offscreen QPA platform: editors are created and read without ever being shown.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonFormModel.hpp"
#include "models/cell_presentation.hpp"
#include "views/JsonCellDelegate.hpp"
#include "views/JsonNumberValidator.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QComboBox>
#include <QLineEdit>
#include <QStyleOptionViewItem>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"text": "hello",
		"count": 42,
		"flag": true,
		"nothing": null,
		"child": { "a": 1 }
	})";

	// Row indices into the form the fixture presents, named so the cases read as kinds rather than as numbers.

	constexpr int STRING_ROW    = 0;
	constexpr int NUMBER_ROW    = 1;
	constexpr int BOOLEAN_ROW   = 2;
	constexpr int NULL_ROW      = 3;
	constexpr int CONTAINER_ROW = 4;
}

class TestJsonCellDelegate : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// The VAL-03 validator.

	void complete_json_numbers_are_acceptable ();
	void partial_json_numbers_are_intermediate ();
	void impossible_input_is_invalid ();
	void fixup_trims_surrounding_whitespace ();

	// Editor creation per value kind.

	void a_string_gets_a_plain_text_editor ();
	void a_number_gets_a_validated_text_editor ();
	void a_boolean_gets_a_two_item_combo ();
	void null_and_container_cells_get_no_editor ();

	// Editor behaviour.

	void an_editor_opens_with_its_content_selected ();
	void an_unacceptable_number_is_not_written_to_the_model ();

private:

	QWidget* create_editor_for ( int row );

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<JsonFormModel>    model;
	std::unique_ptr<JsonCellDelegate> delegate;
	std::unique_ptr<QWidget>          editorParent;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestJsonCellDelegate::init ()
{
	document = std::make_unique<JsonDocument> ();
	undo     = std::make_unique<UndoController> ( document.get () );
	model    = std::make_unique<JsonFormModel> ( document.get (), undo.get () );

	// The form's policy (arrows stay caret keys); the table's differs only in that flag, which the FormView suite
	// covers end to end.

	delegate     = std::make_unique<JsonCellDelegate> ( false );
	editorParent = std::make_unique<QWidget> ();

	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( SAMPLE_DOCUMENT ) );

	document->set_root ( std::move ( result.root ) );

	model->present ( JsonPointer () );
}

void TestJsonCellDelegate::cleanup ()
{
	// Strict reverse dependency order.

	editorParent.reset ();
	delegate.reset ();
	model.reset ();
	undo.reset ();
	document.reset ();
}

QWidget* TestJsonCellDelegate::create_editor_for ( int row )
{
	return delegate->createEditor
	(
		editorParent.get (),
		QStyleOptionViewItem (),
		model->index ( row, JsonFormModel::VALUE_COLUMN )
	);
}

//---------------------------------------------------------------------------------------------------------------------
// The VAL-03 validator
//---------------------------------------------------------------------------------------------------------------------

void TestJsonCellDelegate::complete_json_numbers_are_acceptable ()
{
	const JsonNumberValidator validator;

	const QStringList acceptable { "0", "-0", "42", "-42", "1.5", "-1.50", "1e3", "1E3", "1e+3", "1e-3", "0.0" };

	for ( const QString& candidate : acceptable )
	{
		QString text     = candidate;
		int     position = text.length ();

		QVERIFY2 ( validator.validate ( text, position ) == QValidator::Acceptable, qPrintable ( candidate ) );
	}
}

void TestJsonCellDelegate::partial_json_numbers_are_intermediate ()
{
	// The states a number passes THROUGH while being typed. Each is refused at commit but must be allowed to exist, or
	// the field becomes impossible to type a negative or a decimal into.

	const JsonNumberValidator validator;

	const QStringList intermediate { "", "-", "1.", "-1.", "1e", "1E", "1e+", "1e-", ".", ".5" };

	for ( const QString& candidate : intermediate )
	{
		QString text     = candidate;
		int     position = text.length ();

		QVERIFY2 ( validator.validate ( text, position ) == QValidator::Intermediate, qPrintable ( candidate ) );
	}
}

void TestJsonCellDelegate::impossible_input_is_invalid ()
{
	// Text no amount of further typing can turn into a JSON number, so the keystroke is refused where it is made.
	// "01" belongs here rather than in Intermediate: JSON forbids a leading-zero run, so it is not a prefix of
	// anything valid.

	const JsonNumberValidator validator;

	const QStringList invalid { "abc", "12abc", "1..2", "01", "0x10", "1,5", "--1", "1e2e3" };

	for ( const QString& candidate : invalid )
	{
		QString text     = candidate;
		int     position = text.length ();

		QVERIFY2 ( validator.validate ( text, position ) == QValidator::Invalid, qPrintable ( candidate ) );
	}
}

void TestJsonCellDelegate::fixup_trims_surrounding_whitespace ()
{
	// The one flaw worth repairing silently: it is invisible, and it is what a paste from another application brings.

	const JsonNumberValidator validator;

	QString text = QStringLiteral ( "  42  " );

	validator.fixup ( text );

	QCOMPARE ( text, QStringLiteral ( "42" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Editor creation per value kind
//---------------------------------------------------------------------------------------------------------------------

void TestJsonCellDelegate::a_string_gets_a_plain_text_editor ()
{
	QWidget* const editor = create_editor_for ( STRING_ROW );

	QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor );

	QVERIFY ( textEditor != nullptr );
	QVERIFY ( textEditor->validator () == nullptr );

	delete editor;
}

void TestJsonCellDelegate::a_number_gets_a_validated_text_editor ()
{
	QWidget* const editor = create_editor_for ( NUMBER_ROW );

	QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor );

	QVERIFY ( textEditor != nullptr );
	QVERIFY ( qobject_cast<const JsonNumberValidator*> ( textEditor->validator () ) != nullptr );

	delete editor;
}

void TestJsonCellDelegate::a_boolean_gets_a_two_item_combo ()
{
	// A boolean has exactly two legal values, so offering free text would only create input to reject (EDITOR-02).

	QWidget* const editor = create_editor_for ( BOOLEAN_ROW );

	QComboBox* const booleanEditor = qobject_cast<QComboBox*> ( editor );

	QVERIFY  ( booleanEditor != nullptr );
	QCOMPARE ( booleanEditor->count (), 2 );
	QCOMPARE ( booleanEditor->itemText ( 0 ), cell_text::BOOLEAN_TRUE );
	QCOMPARE ( booleanEditor->itemText ( 1 ), cell_text::BOOLEAN_FALSE );

	delegate->setEditorData ( booleanEditor, model->index ( BOOLEAN_ROW, JsonFormModel::VALUE_COLUMN ) );

	QCOMPARE ( booleanEditor->currentText (), cell_text::BOOLEAN_TRUE );

	delete editor;
}

void TestJsonCellDelegate::null_and_container_cells_get_no_editor ()
{
	// A container drills in (EDITOR-05) and a null placeholder is read-only until EDITOR-12's typed entry lands in
	// Phase 9. Neither opens an editor.

	QVERIFY ( create_editor_for ( NULL_ROW )      == nullptr );
	QVERIFY ( create_editor_for ( CONTAINER_ROW ) == nullptr );
}

//---------------------------------------------------------------------------------------------------------------------
// Editor behaviour
//---------------------------------------------------------------------------------------------------------------------

void TestJsonCellDelegate::an_editor_opens_with_its_content_selected ()
{
	// This is what makes "typing replaces the value" true (EDITOR-02 / 03): Qt forwards the activating keystroke into
	// the editor, and it has to land on a full selection rather than at the end of the existing text.

	QWidget* const editor = create_editor_for ( STRING_ROW );

	delegate->setEditorData ( editor, model->index ( STRING_ROW, JsonFormModel::VALUE_COLUMN ) );

	QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor );

	QCOMPARE ( textEditor->text (), QStringLiteral ( "hello" ) );
	QCOMPARE ( textEditor->selectedText (), QStringLiteral ( "hello" ) );

	delete editor;
}

void TestJsonCellDelegate::an_unacceptable_number_is_not_written_to_the_model ()
{
	// The guard that catches the commit paths Qt takes on its own -- notably focus loss, which does not run through the
	// delegate's key handling. An unacceptable value is simply not written; the cell keeps what it had.

	QWidget* const editor = create_editor_for ( NUMBER_ROW );

	QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor );

	textEditor->setText ( QStringLiteral ( "1e" ) );   // Intermediate: a real halfway state, not junk.

	QVERIFY ( !textEditor->hasAcceptableInput () );

	delegate->setModelData ( editor, model.get (), model->index ( NUMBER_ROW, JsonFormModel::VALUE_COLUMN ) );

	QCOMPARE ( document->resolve ( JsonPointer::parse ( QStringLiteral ( "/count" ) ) )->number_token (),
	           QStringLiteral ( "42" ) );

	delete editor;
}

QTEST_MAIN ( TestJsonCellDelegate )

#include "tst_json_cell_delegate.moc"

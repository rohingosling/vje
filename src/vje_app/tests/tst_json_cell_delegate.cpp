//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include "AppConfig.hpp"
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
#include <QPlainTextEdit>
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

	// Wrapping (SET-05).

	void wrapping_top_aligns_every_cell ();
	void a_wrapped_row_carries_the_same_padding_as_a_single_line_one ();
	void a_narrower_column_gets_a_taller_row ();

	// Regressions for the 2026-07-28 review.

	void a_multi_line_editor_opens_with_its_content_selected ();
	void a_decoded_multi_line_value_gets_a_multi_line_editor ();
	void the_row_height_uses_one_metric_for_both_halves ();

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

	// One delegate serves both grids. It used to take a flag saying whether Left / Right navigated cells; both faces
	// now treat them as caret keys inside an open editor, so there is nothing left to configure (spec EDITOR-02/03).

	delegate     = std::make_unique<JsonCellDelegate> ();
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
	// A container drills in (EDITOR-05), so it opens no editor. A null cell now takes a TYPED ENTRY (EDITOR-12) -- a
	// plain text box with no validator, the same one a missing / provisional cell uses -- so it DOES open one. The
	// delegate is model-agnostic: the object form withholds ItemIsEditable from its null fields (OQ-2), so it never
	// asks; the array table does.

	QWidget* const nullEditor = create_editor_for ( NULL_ROW );

	QVERIFY  ( qobject_cast<QLineEdit*> ( nullEditor ) != nullptr );
	QVERIFY  ( qobject_cast<QLineEdit*> ( nullEditor )->validator () == nullptr );   // Any text is a legal JSON literal.
	QVERIFY  ( create_editor_for ( CONTAINER_ROW ) == nullptr );

	delete nullEditor;
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

//---------------------------------------------------------------------------------------------------------------------
// Wrapping (SET-05)
//
// Both claims live in protected overrides, so the suite reaches them through a subclass that re-exposes them. That is
// deliberate rather than a workaround: the alternative is asserting against rendered pixels, which would pin the
// theme's padding as tightly as the rule under test.
//---------------------------------------------------------------------------------------------------------------------

namespace
{
	class DelegateProbe : public JsonCellDelegate
	{
	public:

		using JsonCellDelegate::JsonCellDelegate;
		using JsonCellDelegate::initStyleOption;
	};
}

void TestJsonCellDelegate::wrapping_top_aligns_every_cell ()
{
	// While wrapping, a row is as tall as its TALLEST cell -- so a one-line key beside a five-line value would float in
	// the middle of it unless every cell is pinned to the top. The key must sit level with the first line of the value
	// it names (spec section 2.10).

	DelegateProbe probe;

	QStyleOptionViewItem option;

	// Not while wrapping is off: with one-line rows the two alignments differ by a couple of pixels of padding, and
	// centred is what every other grid in the application uses.

	probe.initStyleOption ( &option, model->index ( STRING_ROW, JsonFormModel::VALUE_COLUMN ) );

	QCOMPARE ( option.displayAlignment, Qt::AlignLeft | Qt::AlignVCenter );

	probe.set_wrap_strings ( true );

	// Both columns, not just the key: a short value in a tall row must not float either.

	for ( const int column : { JsonFormModel::KEY_COLUMN, JsonFormModel::VALUE_COLUMN } )
	{
		QStyleOptionViewItem wrapped;

		probe.initStyleOption ( &wrapped, model->index ( STRING_ROW, column ) );

		QCOMPARE ( wrapped.displayAlignment, Qt::AlignLeft | Qt::AlignTop );
	}
}

void TestJsonCellDelegate::a_wrapped_row_carries_the_same_padding_as_a_single_line_one ()
{
	// The gap from the bottom of one field to the top of the next must be the SAME everywhere, wrapped or not. Taking
	// the style's measured height directly does not give that: an unwrapped row is font height + ROW_VERTICAL_PADDING,
	// while the style measures only its own focus-frame margin -- a pixel or two smaller, invisible in isolation and
	// obvious in a column where every other gap is the other value. So the height is rebuilt from the row formula, and
	// what is asserted here is the formula rather than a number.

	document->set_root ( JsonParser::parse ( QStringLiteral (
		"{\"long\":\"a value long enough that it must occupy several lines once it is wrapped into a narrow column, "
		"which is what this case needs it to do\"}" ) ).root );

	model->present ( JsonPointer () );

	QStyleOptionViewItem option;

	option.rect        = QRect ( 0, 0, 160, 400 );   // A zero-HEIGHT rect is invalid, and Qt then skips wrapping.
	option.font        = QFont ();
	option.fontMetrics = QFontMetrics ( option.font );
	option.features    = QStyleOptionViewItem::WrapText;

	const QModelIndex cell = model->index ( 0, JsonFormModel::VALUE_COLUMN );

	delegate->set_wrap_strings ( true );

	const int lineHeight = option.fontMetrics.height ();
	const int padding    = config::form::ROW_VERTICAL_PADDING;
	const int height     = delegate->sizeHint ( option, cell ).height ();

	// Whole lines plus exactly one padding -- never a fraction of a line, and never the style's own margin instead.

	QVERIFY2 ( height > lineHeight + padding, "The value did not wrap at this width; the case proves nothing" );
	QCOMPARE ( ( height - padding ) % lineHeight, 0 );

	// And at one line the two paths agree exactly, which is the property that makes every gap in the column equal.

	document->set_root ( JsonParser::parse ( QStringLiteral ( "{\"short\":\"x\"}" ) ).root );

	model->present ( JsonPointer () );

	QCOMPARE ( delegate->sizeHint ( option, model->index ( 0, JsonFormModel::VALUE_COLUMN ) ).height (),
	           lineHeight + padding );
}

void TestJsonCellDelegate::a_narrower_column_gets_a_taller_row ()
{
	// SET-05's promise, and the half a user notices: the whole value is visible at ANY width, so a column that narrows
	// gets a row with more lines rather than a value with its tail cut off. There is no cap -- a cap was tried and
	// removed, because per-pixel vertical scrolling makes a tall row ordinary to scroll through and hiding the end of
	// someone's value to avoid one is the wrong trade.

	document->set_root ( JsonParser::parse ( QStringLiteral (
		"{\"long\":\"%1\"}" ).arg ( QStringLiteral ( "word " ).repeated ( 120 ).trimmed () ) ).root );

	model->present ( JsonPointer () );

	const QModelIndex cell = model->index ( 0, JsonFormModel::VALUE_COLUMN );

	delegate->set_wrap_strings ( true );

	const auto height_at = [ & ] ( int width )
	{
		QStyleOptionViewItem option;

		option.rect        = QRect ( 0, 0, width, 4000 );   // A zero-HEIGHT rect is invalid, and Qt then skips wrapping.
		option.font        = QFont ();
		option.fontMetrics = QFontMetrics ( option.font );
		option.features    = QStyleOptionViewItem::WrapText;

		return delegate->sizeHint ( option, cell ).height ();
	};

	const int wide   = height_at ( 600 );
	const int narrow = height_at ( 150 );

	QVERIFY2 ( narrow > wide, "Narrowing the column did not make the row taller" );

	// Roughly in proportion: a quarter of the width needs about four times the lines. Bounded loosely, because word
	// boundaries make it approximate -- what would fail here is a row that stopped growing, which is the reported bug.

	const int lineHeight  = QFontMetrics ( QFont () ).height ();
	const int narrowLines = ( narrow - config::form::ROW_VERTICAL_PADDING ) / lineHeight;
	const int wideLines   = ( wide   - config::form::ROW_VERTICAL_PADDING ) / lineHeight;

	QVERIFY2 ( narrowLines >= wideLines * 2,
	           qPrintable ( QStringLiteral ( "%1 lines wide, %2 lines narrow" ).arg ( wideLines ).arg ( narrowLines ) ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Regressions for the 2026-07-28 review
//---------------------------------------------------------------------------------------------------------------------

void TestJsonCellDelegate::a_multi_line_editor_opens_with_its_content_selected ()
{
	// setEditorData selected all only in its QLineEdit branch; the multi-line editor fell through to the base, which
	// leaves the caret at offset 0 with no selection. FormGridController makes typing an edit trigger and Qt forwards
	// the activating keystroke in, so typing PREPENDED -- switching Wrap strings on silently changed what typing into
	// a field did (EDITOR-02 / 03).

	delegate->set_wrap_strings ( true );

	QWidget* const editor = create_editor_for ( STRING_ROW );

	QPlainTextEdit* const wrappedEditor = qobject_cast<QPlainTextEdit*> ( editor );

	QVERIFY ( wrappedEditor != nullptr );

	delegate->setEditorData ( wrappedEditor, model->index ( STRING_ROW, JsonFormModel::VALUE_COLUMN ) );

	QCOMPARE ( wrappedEditor->toPlainText (), QStringLiteral ( "hello" ) );

	// The whole content selected, so the activating keystroke replaces it rather than landing in front of it.

	QCOMPARE ( wrappedEditor->textCursor ().selectedText (), QStringLiteral ( "hello" ) );

	delete editor;
}

void TestJsonCellDelegate::a_decoded_multi_line_value_gets_a_multi_line_editor ()
{
	// The editor kind follows the VALUE as well as the row. Keying it on Wrap strings alone left a Decoded value
	// carrying real line breaks in a QLineEdit that could neither show nor navigate it.

	document->set_root ( JsonParser::parse ( QStringLiteral ( "{\"note\":\"line one\\nline two\"}" ) ).root );

	model->present ( JsonPointer () );
	model->set_string_display ( StringDisplay::Decoded );

	delegate->set_wrap_strings ( false );

	QWidget* const editor = create_editor_for ( 0 );

	QVERIFY2 ( qobject_cast<QPlainTextEdit*> ( editor ) != nullptr,
	           "a value with real line breaks opened in a single-line editor" );

	delete editor;

	// And in ESCAPED notation the same value is one line carrying a literal backslash-n, so it keeps its QLineEdit --
	// the test is the edit text, not the stored value.

	model->set_string_display ( StringDisplay::Escaped );

	QWidget* const escapedEditor = create_editor_for ( 0 );

	QVERIFY ( qobject_cast<QLineEdit*> ( escapedEditor ) != nullptr );

	delete escapedEditor;
}

void TestJsonCellDelegate::the_row_height_uses_one_metric_for_both_halves ()
{
	// sizeHint divided the measured height by lineSpacing() and multiplied back by height(). QTextLayout does not add
	// leading, so on any font with leading the count came out LOW and the row was rebuilt a line short, clipping the
	// tail of the value. Asserted as the invariant rather than as a pixel count, since it is invisible on a
	// zero-leading font.

	document->set_root ( JsonParser::parse ( QStringLiteral ( "{\"long\":\"%1\"}" )
	                                         .arg ( QStringLiteral ( "word " ).repeated ( 40 ).trimmed () ) ).root );

	model->present ( JsonPointer () );

	QStyleOptionViewItem option;

	option.rect        = QRect ( 0, 0, 160, 400 );
	option.font        = QFont ();
	option.fontMetrics = QFontMetrics ( option.font );
	option.features    = QStyleOptionViewItem::WrapText;

	delegate->set_wrap_strings ( true );

	const QModelIndex cell    = model->index ( 0, JsonFormModel::VALUE_COLUMN );
	const int         height  = delegate->sizeHint ( option, cell ).height ();
	const int         content = height - config::form::ROW_VERTICAL_PADDING;

	// Whole lines of the SAME metric the height is built from, and enough of them to hold what the style measured.

	QCOMPARE ( content % option.fontMetrics.height (), 0 );

	QVERIFY2 ( content >= QStyledItemDelegate ().sizeHint ( option, cell ).height () - config::form::ROW_VERTICAL_PADDING,
	           "the rebuilt row is shorter than the text the style measured -- the last line would clip" );
}

QTEST_MAIN ( TestJsonCellDelegate )

#include "tst_json_cell_delegate.moc"

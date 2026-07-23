//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonFormModel -- the object form's projection and edit routing (EDITOR-02).
//
//   Three cases here are not arithmetic:
//
//     - The DUPLICATE-KEY GUARD. A loaded file may hold duplicate sibling keys and VJE preserves them, but a JSON
//       Pointer names the FIRST member with a key -- so an edit committed against the second would silently write the
//       first. The model presents both and marks both read-only. Without this test that guard is one refactor away
//       from being "simplified" back into a data-corruption bug.
//     - The LONE SCALAR ROOT, the one scalar that presents itself rather than through a parent.
//     - IN-PLACE updates, including the RELABEL pass: a key rename changes no row's identity, so it is invisible to an
//       identity diff and needs its own handling.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonFormModel.hpp"
#include "models/cell_presentation.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"id": 1001,
		"name": "Alex Rivera",
		"active": true,
		"lastLogin": null,
		"roles": [ "admin" ],
		"profile": { "email": "alex@example.com" }
	})";

	JsonPointer pointer ( const QString& text )
	{
		return JsonPointer::parse ( text );
	}
}

class TestJsonFormModel : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// Projection.

	void an_object_projects_one_row_per_member ();
	void values_show_their_form_presentation ();
	void a_scalar_root_projects_a_single_keyless_row ();
	void an_array_is_refused_because_it_belongs_to_the_table ();

	// Editing.

	void only_scalar_values_are_editable ();
	void every_key_is_renameable_whatever_its_value ();
	void a_rename_reaches_the_document_through_undo ();
	void a_rename_onto_an_existing_key_is_refused ();
	void a_key_cell_names_its_rivals ();
	void a_commit_reaches_the_document_through_undo ();
	void an_invalid_number_is_refused ();
	void duplicate_keys_are_presented_but_read_only ();

	// Incremental updates.

	void a_value_change_patches_one_row_without_resetting ();
	void removing_a_member_removes_exactly_that_row ();
	void renaming_a_key_relabels_without_resetting ();

	// Addressing.

	void rows_and_pointers_round_trip ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	std::unique_ptr<JsonDocument>   document;
	std::unique_ptr<UndoController> undo;
	std::unique_ptr<JsonFormModel>  model;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::init ()
{
	document = std::make_unique<JsonDocument> ();
	undo     = std::make_unique<UndoController> ( document.get () );
	model    = std::make_unique<JsonFormModel> ( document.get (), undo.get () );

	load ( SAMPLE_DOCUMENT );
}

void TestJsonFormModel::cleanup ()
{
	// Strict reverse dependency order.

	model.reset ();
	undo.reset ();
	document.reset ();
}

std::unique_ptr<JsonNode> TestJsonFormModel::parse ( const char* text ) const
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	return std::move ( result.root );
}

void TestJsonFormModel::load ( const char* text )
{
	document->set_root ( parse ( text ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Projection
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::an_object_projects_one_row_per_member ()
{
	model->present ( JsonPointer () );

	QVERIFY  ( model->is_presenting () );
	QCOMPARE ( model->rowCount (),    6 );
	QCOMPARE ( model->columnCount (), JsonFormModel::COLUMN_COUNT );

	// Member ORDER is preserved (FILE-04), so the form reads in the order the file does.

	const QStringList expectedKeys { "id", "name", "active", "lastLogin", "roles", "profile" };

	for ( int row = 0; row < expectedKeys.size (); ++row )
	{
		QCOMPARE ( model->index ( row, JsonFormModel::KEY_COLUMN ).data ().toString (), expectedKeys.at ( row ) );
	}
}

void TestJsonFormModel::values_show_their_form_presentation ()
{
	model->present ( JsonPointer () );

	auto value_text = [ this ] ( int row )
	{
		return model->index ( row, JsonFormModel::VALUE_COLUMN ).data ().toString ();
	};

	QCOMPARE ( value_text ( 0 ), QStringLiteral ( "1001" ) );          // number, raw token
	QCOMPARE ( value_text ( 1 ), QStringLiteral ( "Alex Rivera" ) );   // string, unquoted
	QCOMPARE ( value_text ( 2 ), cell_text::BOOLEAN_TRUE );
	QCOMPARE ( value_text ( 3 ), cell_text::NULL_PLACEHOLDER );
	QCOMPARE ( value_text ( 4 ), cell_text::ARRAY_PLACEHOLDER );
	QCOMPARE ( value_text ( 5 ), cell_text::OBJECT_PLACEHOLDER );
}

void TestJsonFormModel::a_scalar_root_projects_a_single_keyless_row ()
{
	// EDITOR-02's one exception: a scalar SELECTION normally presents its parent, but a scalar document root has no
	// parent, so it renders as a lone single-row form.

	load ( R"("just a string")" );

	model->present ( JsonPointer () );

	QVERIFY  ( model->is_presenting () );
	QCOMPARE ( model->rowCount (), 1 );

	QCOMPARE ( model->index ( 0, JsonFormModel::KEY_COLUMN ).data ().toString (), QString () );
	QCOMPARE ( model->index ( 0, JsonFormModel::VALUE_COLUMN ).data ().toString (),
	           QStringLiteral ( "just a string" ) );

	QCOMPARE ( model->pointer_for_row ( 0 ).to_string (), QString () );
}

void TestJsonFormModel::an_array_is_refused_because_it_belongs_to_the_table ()
{
	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QVERIFY  ( !model->is_presenting () );
	QCOMPARE ( model->rowCount (), 0 );
}

//---------------------------------------------------------------------------------------------------------------------
// Editing
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::only_scalar_values_are_editable ()
{
	model->present ( JsonPointer () );

	QVERIFY (  model->flags ( model->index ( 0, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY (  model->flags ( model->index ( 2, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	QVERIFY ( !model->flags ( model->index ( 3, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 4, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 5, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
}

// The key column asks a DIFFERENT question from the value column, and the difference is the point: a null's value does
// not open an editor and an array's value drills in, but the key naming either renames perfectly well (EDIT-02).

void TestJsonFormModel::every_key_is_renameable_whatever_its_value ()
{
	model->present ( JsonPointer () );

	for ( int row = 0; row < model->rowCount (); ++row )
	{
		QVERIFY2
		(
			model->flags ( model->index ( row, JsonFormModel::KEY_COLUMN ) ).testFlag ( Qt::ItemIsEditable ),
			qPrintable ( QStringLiteral ( "row %1 (%2) should be renameable" )
			             .arg ( row ).arg ( model->key_for_row ( row ) ) )
		);
	}

	// lastLogin is null and roles is an array -- neither value is editable, and both keys are.

	QVERIFY ( !model->flags ( model->index ( 3, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 4, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
}

void TestJsonFormModel::a_rename_reaches_the_document_through_undo ()
{
	model->present ( JsonPointer () );

	QVERIFY ( model->setData ( model->index ( 1, JsonFormModel::KEY_COLUMN ),
	                           QStringLiteral ( "fullName" ), Qt::EditRole ) );

	QCOMPARE ( model->key_for_row ( 1 ), QStringLiteral ( "fullName" ) );

	// The value travelled with its key rather than being rebuilt beside it.

	QCOMPARE ( model->data ( model->index ( 1, JsonFormModel::VALUE_COLUMN ), Qt::DisplayRole ).toString (),
	           QStringLiteral ( "Alex Rivera" ) );
}

// VAL-02, asserted through setData because that is where the document changes -- a guard that only lived in flags()
// would be decoration.

void TestJsonFormModel::a_rename_onto_an_existing_key_is_refused ()
{
	model->present ( JsonPointer () );

	QVERIFY ( !model->setData ( model->index ( 1, JsonFormModel::KEY_COLUMN ),
	                            QStringLiteral ( "active" ), Qt::EditRole ) );

	QCOMPARE ( model->key_for_row ( 1 ), QStringLiteral ( "name" ) );
}

// The rival set the editor validates against: every other key, and never the row's own -- leaving a name unchanged has
// to stay committable.

void TestJsonFormModel::a_key_cell_names_its_rivals ()
{
	model->present ( JsonPointer () );

	const QStringList rivals = model->data ( model->index ( 1, JsonFormModel::KEY_COLUMN ),
	                                         cell_roles::RIVAL_KEYS ).toStringList ();

	QVERIFY  ( !rivals.contains ( QStringLiteral ( "name" ) ) );
	QVERIFY  (  rivals.contains ( QStringLiteral ( "active" ) ) );
	QCOMPARE (  rivals.size (), model->rowCount () - 1 );

	QVERIFY ( model->data ( model->index ( 1, JsonFormModel::KEY_COLUMN ), cell_roles::IS_KEY_CELL ).toBool () );
	QVERIFY ( !model->data ( model->index ( 1, JsonFormModel::VALUE_COLUMN ), cell_roles::IS_KEY_CELL ).toBool () );
}

void TestJsonFormModel::a_commit_reaches_the_document_through_undo ()
{
	model->present ( JsonPointer () );

	QVERIFY ( model->setData ( model->index ( 1, JsonFormModel::VALUE_COLUMN ),
	                           QStringLiteral ( "Sam Rivera" ), Qt::EditRole ) );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/name" ) ) )->string_value (),
	           QStringLiteral ( "Sam Rivera" ) );

	QVERIFY ( undo->can_undo () );
}

void TestJsonFormModel::an_invalid_number_is_refused ()
{
	model->present ( JsonPointer () );

	QVERIFY ( !model->setData ( model->index ( 0, JsonFormModel::VALUE_COLUMN ),
	                            QStringLiteral ( "1..2" ), Qt::EditRole ) );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/id" ) ) )->number_token (),
	           QStringLiteral ( "1001" ) );
}

void TestJsonFormModel::duplicate_keys_are_presented_but_read_only ()
{
	// RFC 8259 permits duplicate keys and a loaded file keeps them (FILE-04, VAL-02). A pointer resolves to the FIRST
	// match, so committing against the second would write the first -- silently, and with a plausible-looking undo
	// entry. Both are shown; neither is editable. Document > Rename Key is the repair.

	load ( R"({ "name": "first", "name": "second", "other": 1 })" );

	model->present ( JsonPointer () );

	QCOMPARE ( model->rowCount (), 3 );

	QCOMPARE ( model->index ( 0, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "first" ) );
	QCOMPARE ( model->index ( 1, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "second" ) );

	QVERIFY ( !model->flags ( model->index ( 0, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 1, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	// The unduplicated sibling is unaffected -- the guard is per key, not per object.

	QVERIFY ( model->flags ( model->index ( 2, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	QVERIFY ( !model->setData ( model->index ( 1, JsonFormModel::VALUE_COLUMN ),
	                            QStringLiteral ( "changed" ), Qt::EditRole ) );

	QCOMPARE ( model->index ( 0, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "first" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Incremental updates
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::a_value_change_patches_one_row_without_resetting ()
{
	model->present ( JsonPointer () );

	QSignalSpy resetSpy   ( model.get (), &QAbstractItemModel::modelAboutToBeReset );
	QSignalSpy changedSpy ( model.get (), &QAbstractItemModel::dataChanged );

	QVERIFY ( model->setData ( model->index ( 1, JsonFormModel::VALUE_COLUMN ),
	                           QStringLiteral ( "Sam" ), Qt::EditRole ) );

	QCOMPARE ( resetSpy.count (),   0 );
	QCOMPARE ( changedSpy.count (), 1 );

	QCOMPARE ( changedSpy.at ( 0 ).at ( 0 ).value<QModelIndex> ().row (), 1 );
}

void TestJsonFormModel::removing_a_member_removes_exactly_that_row ()
{
	model->present ( JsonPointer () );

	QSignalSpy resetSpy   ( model.get (), &QAbstractItemModel::modelAboutToBeReset );
	QSignalSpy removedSpy ( model.get (), &QAbstractItemModel::rowsRemoved );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/active" ) ) ), EditOutcome::Applied );

	QCOMPARE ( resetSpy.count (),   0 );
	QCOMPARE ( removedSpy.count (), 1 );

	QCOMPARE ( removedSpy.at ( 0 ).at ( 1 ).toInt (), 2 );

	QCOMPARE ( model->rowCount (), 5 );
	QCOMPARE ( model->index ( 2, JsonFormModel::KEY_COLUMN ).data ().toString (), QStringLiteral ( "lastLogin" ) );
}

void TestJsonFormModel::renaming_a_key_relabels_without_resetting ()
{
	// A rename changes no row's IDENTITY -- the value node is the same object -- so an identity diff sees nothing. The
	// relabel pass is what makes it visible, and this is the case that would silently stop working without it.

	model->present ( JsonPointer () );

	QSignalSpy resetSpy ( model.get (), &QAbstractItemModel::modelAboutToBeReset );

	QCOMPARE ( undo->rename_key ( pointer ( QStringLiteral ( "/name" ) ), QStringLiteral ( "fullName" ) ),
	           EditOutcome::Applied );

	QCOMPARE ( resetSpy.count (), 0 );

	QCOMPARE ( model->index ( 1, JsonFormModel::KEY_COLUMN ).data ().toString (), QStringLiteral ( "fullName" ) );
	QCOMPARE ( model->pointer_for_row ( 1 ).to_string (), QStringLiteral ( "/fullName" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Addressing
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::rows_and_pointers_round_trip ()
{
	model->present ( JsonPointer () );

	QCOMPARE ( model->pointer_for_row ( 2 ).to_string (), QStringLiteral ( "/active" ) );
	QCOMPARE ( model->row_for_pointer ( pointer ( QStringLiteral ( "/active" ) ) ), 2 );

	// A pointer outside this form -- deeper, or elsewhere -- is not one of its rows.

	QCOMPARE ( model->row_for_pointer ( pointer ( QStringLiteral ( "/profile/email" ) ) ), -1 );
	QCOMPARE ( model->row_for_pointer ( JsonPointer () ), -1 );

	// Both columns of a row still answer with the row's VALUE pointer -- that is what makes the key's right-click menu
	// act on the member it names.

	QCOMPARE ( model->grid_pointer ( 2, JsonFormModel::KEY_COLUMN ).to_string (), QStringLiteral ( "/active" ) );

	// But a gesture now edits the cell it was made on, in either column. The redirect that used to send a gesture on
	// the key to its value would put renaming out of reach of the gestures that perform it (EDIT-02).

	QCOMPARE ( model->grid_edit_cell ( 2, JsonFormModel::KEY_COLUMN   ).column, JsonFormModel::KEY_COLUMN );
	QCOMPARE ( model->grid_edit_cell ( 2, JsonFormModel::VALUE_COLUMN ).column, JsonFormModel::VALUE_COLUMN );
}

QTEST_MAIN ( TestJsonFormModel )

#include "tst_json_form_model.moc"

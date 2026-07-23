//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonTableModel -- the array table's projection, its edit routing, and its
//   incremental updates (EDITOR-03).
//
//   The cases that carry weight, as distinct from the ones that merely check arithmetic:
//
//     - The COLUMN PROJECTION, including both of its boundaries: a ragged array of objects is a proper table with
//       MISSING cells, while an array of MIXED kinds is deliberately single-column instead. Those are different
//       conditions with different answers, and confusing them is the most likely way this model goes wrong.
//     - IN-PLACE updates. A value commit must emit dataChanged for exactly one cell and must NOT reset the model --
//       a reset is what would throw away column widths, the current cell, and the scroll position (EDITOR-03).
//       The model-reset spies are the real assertion; the dataChanged counts are how the claim is made specific.
//     - The EDIT GATE. An invalid number is refused by the model (VAL-03), which is what leaves the view's editor open
//       on the errored cell instead of silently reverting it.
//
//   Driven through a real UndoController rather than a fake, so what is asserted is that the table actually edits the
//   document the way the rest of the application would.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTableModel.hpp"
#include "models/cell_presentation.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <memory>

using namespace vje;

namespace
{
	// One document carrying every projection case: a scalar array, a ragged object array, a mixed array, and an empty
	// one -- so a single fixture covers the whole of the column rule.

	const char* const SAMPLE_DOCUMENT = R"({
		"roles": [ "admin", "editor" ],
		"projects":
		[
			{ "name": "JSON Editor",    "status": "in-progress", "priority": 1,    "tags": [ "ui" ] },
			{ "name": "Data Migration", "priority": 2,            "extra": null,   "nested": { "a": 1 } }
		],
		"mixed": [ 1, { "a": 2 } ],
		"empty": []
	})";

	JsonPointer pointer ( const QString& text )
	{
		return JsonPointer::parse ( text );
	}
}

class TestJsonTableModel : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// Projection.

	void a_scalar_array_projects_one_value_column ();
	void an_object_array_projects_the_first_encountered_key_union ();
	void a_ragged_element_yields_missing_cells ();
	void a_mixed_kind_array_falls_back_to_one_column ();
	void an_empty_array_projects_no_rows ();

	// Display.

	void containers_and_null_show_their_placeholders ();
	void a_number_keeps_its_raw_token ();

	// Addressing.

	void pointers_and_cells_round_trip ();

	// Editing.

	void only_scalar_cells_are_editable ();
	void a_string_commit_reaches_the_document ();
	void a_boolean_commits_from_its_display_text ();
	void an_invalid_number_is_refused_and_changes_nothing ();

	// Incremental updates.

	void a_value_commit_patches_one_cell_without_resetting ();
	void removing_an_element_removes_exactly_that_row ();
	void inserting_an_element_inserts_exactly_that_row ();
	void replacing_the_projected_array_rebuilds_it ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	std::unique_ptr<JsonDocument>   document;
	std::unique_ptr<UndoController> undo;
	std::unique_ptr<JsonTableModel> model;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::init ()
{
	document = std::make_unique<JsonDocument> ();
	undo     = std::make_unique<UndoController> ( document.get () );
	model    = std::make_unique<JsonTableModel> ( document.get (), undo.get () );

	load ( SAMPLE_DOCUMENT );
}

void TestJsonTableModel::cleanup ()
{
	// Strict reverse dependency order: both the model and the undo controller write through the
	// document in their destructors' reach, so the document goes last.

	model.reset ();
	undo.reset ();
	document.reset ();
}

std::unique_ptr<JsonNode> TestJsonTableModel::parse ( const char* text ) const
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	return std::move ( result.root );
}

void TestJsonTableModel::load ( const char* text )
{
	document->set_root ( parse ( text ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Projection
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::a_scalar_array_projects_one_value_column ()
{
	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QVERIFY  ( model->is_presenting () );
	QVERIFY  ( !model->is_object_table () );
	QCOMPARE ( model->rowCount (),    2 );
	QCOMPARE ( model->columnCount (), 1 );

	QCOMPARE ( model->index ( 0, 0 ).data ().toString (), QStringLiteral ( "admin" ) );
	QCOMPARE ( model->index ( 1, 0 ).data ().toString (), QStringLiteral ( "editor" ) );

	// The single column is named after the array itself, which is more use than a generic label.

	QCOMPARE
	(
		model->headerData ( 0, Qt::Horizontal, Qt::DisplayRole ).toString (),
		QStringLiteral ( "roles" )
	);
}

void TestJsonTableModel::an_object_array_projects_the_first_encountered_key_union ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QVERIFY  ( model->is_object_table () );
	QCOMPARE ( model->rowCount (), 2 );

	// First-encountered order, NOT alphabetical: the first element's keys in document order, then whatever the second
	// adds, appended. That is what keeps a ragged table reading like the document it came from (EDITOR-03).

	const QStringList expectedColumns { "name", "status", "priority", "tags", "extra", "nested" };

	QCOMPARE ( model->columnCount (), expectedColumns.size () );

	for ( int column = 0; column < expectedColumns.size (); ++column )
	{
		QCOMPARE
		(
			model->headerData ( column, Qt::Horizontal, Qt::DisplayRole ).toString (),
			expectedColumns.at ( column )
		);
	}
}

void TestJsonTableModel::a_ragged_element_yields_missing_cells ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	// Element 1 has no "status". The cell exists, is landable, and shows nothing -- it is MISSING, which is a different
	// thing from holding null (element 1's "extra" is the null case, checked below).

	const QModelIndex missingCell = model->index ( 1, 1 );

	QCOMPARE ( missingCell.data ( cell_roles::CONTENT_KIND ).toInt (), static_cast<int> ( CellContent::Missing ) );
	QCOMPARE ( missingCell.data ().toString (), QString () );

	// It carries the pointer its member WOULD occupy, which is what makes a create-the-member paste addressable in
	// Phase 9 (EDITOR-11).

	QCOMPARE ( model->pointer_for_cell ( 1, 1 ).to_string (), QStringLiteral ( "/projects/1/status" ) );

	// And element 0 has no "extra" -- the union works in both directions.

	QCOMPARE
	(
		model->index ( 0, 4 ).data ( cell_roles::CONTENT_KIND ).toInt (),
		static_cast<int> ( CellContent::Missing )
	);
}

void TestJsonTableModel::a_mixed_kind_array_falls_back_to_one_column ()
{
	// The rule the spec leaves open, pinned. An array of objects with DIFFERING KEYS is ragged and still a proper table
	// (above); an array of differing KINDS has no honest column set, so it renders single-column with the object
	// element showing as "{...}" -- landable and drillable, and repairable by EDIT-11.

	model->present ( pointer ( QStringLiteral ( "/mixed" ) ) );

	QVERIFY  ( !model->is_object_table () );
	QCOMPARE ( model->columnCount (), 1 );
	QCOMPARE ( model->rowCount (),    2 );

	QCOMPARE ( model->index ( 0, 0 ).data ().toString (), QStringLiteral ( "1" ) );
	QCOMPARE ( model->index ( 1, 0 ).data ().toString (), cell_text::OBJECT_PLACEHOLDER );
}

void TestJsonTableModel::an_empty_array_projects_no_rows ()
{
	model->present ( pointer ( QStringLiteral ( "/empty" ) ) );

	QVERIFY  ( model->is_presenting () );
	QCOMPARE ( model->rowCount (), 0 );

	// Still ONE column, not zero: an empty array presents a real (if empty) grid, which is what EDITOR-12's
	// already-in-place provisional row will be typed into.

	QCOMPARE ( model->columnCount (), 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// Display
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::containers_and_null_show_their_placeholders ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QCOMPARE ( model->index ( 0, 3 ).data ().toString (), cell_text::ARRAY_PLACEHOLDER );    // tags: [ "ui" ]
	QCOMPARE ( model->index ( 1, 5 ).data ().toString (), cell_text::OBJECT_PLACEHOLDER );   // nested: { "a": 1 }
	QCOMPARE ( model->index ( 1, 4 ).data ().toString (), cell_text::NULL_PLACEHOLDER );     // extra: null

	// Null and missing are both placeholders but are NOT the same cell kind -- only the missing one creates a member
	// when written to (EDITOR-11 / 12).

	QCOMPARE
	(
		model->index ( 1, 4 ).data ( cell_roles::CONTENT_KIND ).toInt (),
		static_cast<int> ( CellContent::Null )
	);
}

void TestJsonTableModel::a_number_keeps_its_raw_token ()
{
	// FILE-10: a table round trip must not quietly rewrite 1.50 as 1.5 or 1e3 as 1000.

	load ( R"({ "values": [ 1.50, 1e3, -0.0 ] })" );

	model->present ( pointer ( QStringLiteral ( "/values" ) ) );

	QCOMPARE ( model->index ( 0, 0 ).data ().toString (), QStringLiteral ( "1.50" ) );
	QCOMPARE ( model->index ( 1, 0 ).data ().toString (), QStringLiteral ( "1e3" ) );
	QCOMPARE ( model->index ( 2, 0 ).data ().toString (), QStringLiteral ( "-0.0" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Addressing
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::pointers_and_cells_round_trip ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QCOMPARE ( model->pointer_for_cell ( 1, 2 ).to_string (), QStringLiteral ( "/projects/1/priority" ) );

	const GridPosition cell = model->cell_for_pointer ( pointer ( QStringLiteral ( "/projects/1/priority" ) ) );

	QCOMPARE ( cell.row,    1 );
	QCOMPARE ( cell.column, 2 );

	// A pointer to the array itself, or to something inside a container CELL, is not a cell of this table.

	QVERIFY ( !model->cell_for_pointer ( pointer ( QStringLiteral ( "/projects" ) ) ).is_valid () );
	QVERIFY ( !model->cell_for_pointer ( pointer ( QStringLiteral ( "/projects/0/tags/0" ) ) ).is_valid () );
	QVERIFY ( !model->cell_for_pointer ( pointer ( QStringLiteral ( "/roles/0" ) ) ).is_valid () );

	// The single-column form addresses a cell one level down instead of two.

	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QCOMPARE ( model->cell_for_pointer ( pointer ( QStringLiteral ( "/roles/1" ) ) ).row, 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// Editing
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::only_scalar_cells_are_editable ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	// Everything is SELECTABLE -- the landability rule (EDITOR-03) -- but only a scalar opens an editor.

	const QList<QModelIndex> everyKind
	{
		model->index ( 0, 0 ),   // string
		model->index ( 0, 2 ),   // number
		model->index ( 0, 3 ),   // array  ({...} drill-in)
		model->index ( 1, 4 ),   // null   (read-only placeholder until EDITOR-12)
		model->index ( 1, 1 )    // missing
	};

	for ( const QModelIndex& index : everyKind )
	{
		QVERIFY2 ( model->flags ( index ).testFlag ( Qt::ItemIsSelectable ), "a cell was not landable" );
	}

	QVERIFY (  model->flags ( model->index ( 0, 0 ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY (  model->flags ( model->index ( 0, 2 ) ).testFlag ( Qt::ItemIsEditable ) );

	QVERIFY ( !model->flags ( model->index ( 0, 3 ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 1, 4 ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 1, 1 ) ).testFlag ( Qt::ItemIsEditable ) );
}

void TestJsonTableModel::a_string_commit_reaches_the_document ()
{
	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QVERIFY ( model->setData ( model->index ( 0, 0 ), QStringLiteral ( "Renamed" ), Qt::EditRole ) );

	QCOMPARE
	(
		document->resolve ( pointer ( QStringLiteral ( "/projects/0/name" ) ) )->string_value (),
		QStringLiteral ( "Renamed" )
	);

	// Through the undo stack, not around it -- one commit is one undoable step (EDIT-01).

	QVERIFY ( undo->can_undo () );

	undo->undo ();

	QCOMPARE
	(
		document->resolve ( pointer ( QStringLiteral ( "/projects/0/name" ) ) )->string_value (),
		QStringLiteral ( "JSON Editor" )
	);
}

void TestJsonTableModel::a_boolean_commits_from_its_display_text ()
{
	// The boolean editor is a combo showing "true" / "false", so the model has to accept that spelling as well as a
	// real bool -- the delegate is free to send either.

	load ( R"({ "flags": [ true, false ] })" );

	model->present ( pointer ( QStringLiteral ( "/flags" ) ) );

	QVERIFY ( model->setData ( model->index ( 0, 0 ), QStringLiteral ( "false" ), Qt::EditRole ) );
	QVERIFY ( !document->resolve ( pointer ( QStringLiteral ( "/flags/0" ) ) )->boolean_value () );

	QVERIFY ( model->setData ( model->index ( 1, 0 ), true, Qt::EditRole ) );
	QVERIFY ( document->resolve ( pointer ( QStringLiteral ( "/flags/1" ) ) )->boolean_value () );
}

void TestJsonTableModel::an_invalid_number_is_refused_and_changes_nothing ()
{
	// VAL-03. Returning false is what keeps the view's editor open on the errored cell rather than reverting it
	// silently (EDITOR-03).

	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QVERIFY ( !model->setData ( model->index ( 0, 2 ), QStringLiteral ( "12abc" ), Qt::EditRole ) );

	QCOMPARE
	(
		document->resolve ( pointer ( QStringLiteral ( "/projects/0/priority" ) ) )->number_token (),
		QStringLiteral ( "1" )
	);

	QVERIFY ( !undo->can_undo () );
}

//---------------------------------------------------------------------------------------------------------------------
// Incremental updates
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTableModel::a_value_commit_patches_one_cell_without_resetting ()
{
	// The heart of EDITOR-03's "a cell commit refreshes values in the existing table rather than rebuilding it". A
	// reset here is what would throw away column widths, the current cell, and the scroll position.

	model->present ( pointer ( QStringLiteral ( "/projects" ) ) );

	QSignalSpy resetSpy   ( model.get (), &QAbstractItemModel::modelAboutToBeReset );
	QSignalSpy changedSpy ( model.get (), &QAbstractItemModel::dataChanged );

	QVERIFY ( model->setData ( model->index ( 0, 0 ), QStringLiteral ( "Renamed" ), Qt::EditRole ) );

	QCOMPARE ( resetSpy.count (),   0 );
	QCOMPARE ( changedSpy.count (), 1 );

	// And it named exactly the one cell that changed.

	const QModelIndex changedTopLeft     = changedSpy.at ( 0 ).at ( 0 ).value<QModelIndex> ();
	const QModelIndex changedBottomRight = changedSpy.at ( 0 ).at ( 1 ).value<QModelIndex> ();

	QCOMPARE ( changedTopLeft,     model->index ( 0, 0 ) );
	QCOMPARE ( changedBottomRight, model->index ( 0, 0 ) );
}

void TestJsonTableModel::removing_an_element_removes_exactly_that_row ()
{
	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QSignalSpy resetSpy   ( model.get (), &QAbstractItemModel::modelAboutToBeReset );
	QSignalSpy removedSpy ( model.get (), &QAbstractItemModel::rowsRemoved );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/roles/0" ) ) ), EditOutcome::Applied );

	QCOMPARE ( resetSpy.count (),   0 );
	QCOMPARE ( removedSpy.count (), 1 );

	QCOMPARE ( removedSpy.at ( 0 ).at ( 1 ).toInt (), 0 );   // first
	QCOMPARE ( removedSpy.at ( 0 ).at ( 2 ).toInt (), 0 );   // last

	QCOMPARE ( model->rowCount (), 1 );
	QCOMPARE ( model->index ( 0, 0 ).data ().toString (), QStringLiteral ( "editor" ) );
}

void TestJsonTableModel::inserting_an_element_inserts_exactly_that_row ()
{
	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QSignalSpy resetSpy    ( model.get (), &QAbstractItemModel::modelAboutToBeReset );
	QSignalSpy insertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );

	QCOMPARE
	(
		undo->add_child ( pointer ( QStringLiteral ( "/roles" ) ), JsonKind::String ),
		EditOutcome::Applied
	);

	QCOMPARE ( resetSpy.count (),    0 );
	QCOMPARE ( insertedSpy.count (), 1 );

	QCOMPARE ( insertedSpy.at ( 0 ).at ( 1 ).toInt (), 2 );

	QCOMPARE ( model->rowCount (), 3 );
}

void TestJsonTableModel::replacing_the_projected_array_rebuilds_it ()
{
	// The one change that genuinely cannot be patched: every cell means something different afterwards, so a reset is
	// the honest answer rather than a diff that would lie about identity.

	model->present ( pointer ( QStringLiteral ( "/roles" ) ) );

	QSignalSpy resetSpy ( model.get (), &QAbstractItemModel::modelAboutToBeReset );

	QCOMPARE
	(
		undo->replace_subtree
		(
			pointer ( QStringLiteral ( "/roles" ) ),
			parse ( R"([ { "a": 1 } ])" ),
			QStringLiteral ( "Replace" )
		),
		EditOutcome::Applied
	);

	QCOMPARE ( resetSpy.count (), 1 );

	// And it re-derived the projection, not just the rows: the array is now all-objects, so it is a per-key table.

	QVERIFY  ( model->is_object_table () );
	QCOMPARE ( model->columnCount (), 1 );
	QCOMPARE ( model->headerData ( 0, Qt::Horizontal, Qt::DisplayRole ).toString (), QStringLiteral ( "a" ) );
}

QTEST_MAIN ( TestJsonTableModel )

#include "tst_json_table_model.moc"

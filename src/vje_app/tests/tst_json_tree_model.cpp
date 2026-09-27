//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonTreeModel: the projection's shape and labels (TREE-01/02), the per-type
//   icon mapping (TREE-03), the pointer <-> index correspondence the whole application navigates by, lazy population
//   (TREE-08), and -- the substance of the suite -- INCREMENTAL PATCHING (TREE-07).
//
//   The patching cases deliberately drive REAL edit commands through UndoController rather than poking the document
//   directly. The model's whole reason for existing is that it must stay correct against the change signals those
//   commands actually emit, after the fact and naming only the container; a test that emitted hand-made signals would
//   verify the model against this test's idea of the protocol instead of against the protocol.
//
//   Every structural case asserts on three things: the resulting SHAPE, the exact model SIGNALS emitted (an insert must
//   arrive as rowsInserted, not as a reset -- a reset would pass a shape assertion while throwing away the user's
//   expansion and position), and IDENTITY, via QPersistentModelIndex on a node that must survive the edit.
//
//   Runs under the offscreen QPA platform: QAbstractItemModel is Qt Core, but QModelIndex-heavy tests are simpler and
//   more representative with a QApplication present. The icon library is passed as null, so no palette or icon resource
//   is involved -- the TREE-03 mapping is asserted through the static, pure type_icon_name().
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTreeModel.hpp"
#include "services/IconLibrary.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonSerializer.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QAbstractItemModel>
#include <QMimeData>
#include <QSignalSpy>
#include <QUrl>

#include <memory>

using namespace vje;

namespace
{
	// The document every case starts from unless it says otherwise: an object root with a scalar, a nested object, and
	// an array of mixed kinds, which between them exercise every label and icon rule.

	const char* const SAMPLE_DOCUMENT = R"({
		"name": "vje",
		"meta": { "version": "2.0.0", "stable": true },
		"items": [ 10, "two", null ]
	})";

	JsonPointer pointer ( const QString& text )
	{
		return JsonPointer::parse ( text );
	}
}

class TestJsonTreeModel : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// Projection.

	void empty_document_has_no_rows ();
	void root_item_is_the_file ();
	void root_label_follows_the_file_path ();
	void object_members_show_keys ();
	void array_elements_show_bracketed_indexes ();
	void scalar_values_are_not_in_the_label ();
	void scalar_values_are_in_the_accessible_name ();
	void type_icon_names_cover_every_kind ();
	void branches_report_children_without_materializing ();

	// Pointer correspondence.

	void pointer_round_trips_through_index ();
	void index_for_pointer_materializes_ancestors ();
	void index_for_pointer_rejects_an_unresolvable_pointer ();
	void nearest_index_falls_back_to_the_surviving_ancestor ();

	// Incremental patching (TREE-07).

	void value_edit_emits_data_changed_only ();
	void rename_relabels_without_restructuring ();
	void add_child_inserts_one_row_and_keeps_identity ();
	void delete_removes_one_row_and_keeps_identity ();
	void array_insert_relabels_the_following_siblings ();
	void move_reorders_without_rebuilding ();
	void change_of_type_replaces_the_subtree ();
	void root_commit_rebuilds_and_announces_it ();
	void unmaterialized_branch_is_not_patched ();
	void undo_restores_the_projection ();

	// Document lifecycle.

	void document_load_resets_the_model ();

	// Drag and drop (EDIT-10, Phase 15f).

	void the_root_cannot_start_a_drag_and_a_container_can_receive_one ();
	void a_drag_carries_its_rows_parent_and_nothing_else ();
	void a_drop_is_accepted_only_between_its_own_parents_children ();
	void a_foreign_drag_is_refused_so_it_falls_through_to_the_window ();
	void move_action_is_not_offered_at_all ();

	void deleting_the_second_of_two_duplicate_keys_deletes_that_one ();

	void an_edit_marks_the_changed_row_and_its_ancestors ();
	void the_file_node_only_scope_marks_the_root_alone ();
	void a_scope_change_repaints_every_projected_row ();
	void a_clean_document_repaints_its_marks_away ();
	void the_tooltip_says_where_what_and_whether_changed ();
	void the_file_node_s_tooltip_is_its_full_path ();
	void the_accessible_name_says_a_row_has_unsaved_changes ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	QModelIndex root_index () const;

	// The projection rendered as "path -> label" lines, which makes a shape assertion readable as a whole rather than
	// as a dozen separate index lookups.

	QStringList projection () const;
	void        append_projection ( const QModelIndex& index, const QString& prefix, QStringList& outLines ) const;

	// The rows a dataChanged spy saw announced for `role`, as pointers ("(root)" for the file node), sorted and
	// without repeats. Splitting by role is what lets a case say "only this row's LABEL changed" while the change marks
	// are repainted along its ancestors.

	QStringList rows_changed ( const QSignalSpy& spy, int role ) const;

	// Every projected row CHANGE_MARK_ROLE marks, as pointers.

	QStringList marked_rows () const;

	std::unique_ptr<JsonDocument>   document;
	std::unique_ptr<UndoController> undo;
	std::unique_ptr<JsonTreeModel>  model;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::init ()
{
	document = std::make_unique<JsonDocument> ();
	undo     = std::make_unique<UndoController> ( document.get () );
	model    = std::make_unique<JsonTreeModel> ( document.get (), nullptr );
}

void TestJsonTreeModel::cleanup ()
{
	// Tear down in strict REVERSE dependency order, and do it here rather than by reassigning in init().
	//
	// Both collaborators hold a bare pointer to the document, and UndoController's destructor is not passive: its
	// QUndoStack clears on destruction, which emits cleanChanged, whose handler writes the document's dirty flag. Let
	// the document go first and that write lands on freed memory. Reassigning the members in init() did exactly that,
	// and it read as a model bug -- it crashed only in the test AFTER the first one to push a command, and only on
	// Linux, because Windows happened to leave the freed object intact enough to survive the write.

	model.reset ();
	undo.reset ();
	document.reset ();
}

std::unique_ptr<JsonNode> TestJsonTreeModel::parse ( const char* text ) const
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	return std::move ( result.root );
}

void TestJsonTreeModel::load ( const char* text )
{
	document->set_root ( parse ( text ) );
}

QModelIndex TestJsonTreeModel::root_index () const
{
	return model->index ( 0, 0 );
}

QStringList TestJsonTreeModel::rows_changed ( const QSignalSpy& spy, int role ) const
{
	QStringList rows;

	for ( const QList<QVariant>& emission : spy )
	{
		const QModelIndex topLeft     = emission.at ( 0 ).value<QModelIndex> ();
		const QModelIndex bottomRight = emission.at ( 1 ).value<QModelIndex> ();
		const QList<int>  roles       = emission.at ( 2 ).value<QList<int>> ();

		if ( !roles.contains ( role ) )
		{
			continue;
		}

		for ( int row = topLeft.row (); row <= bottomRight.row (); ++row )
		{
			const QString text = model->pointer_for_index ( topLeft.siblingAtRow ( row ) ).to_string ();

			rows.append ( text.isEmpty () ? QStringLiteral ( "(root)" ) : text );
		}
	}

	rows.removeDuplicates ();
	rows.sort ();

	return rows;
}

QStringList TestJsonTreeModel::marked_rows () const
{
	QStringList rows;
	QList<QModelIndex> pending { root_index () };

	while ( !pending.isEmpty () )
	{
		const QModelIndex index = pending.takeFirst ();

		if ( model->data ( index, JsonTreeModel::CHANGE_MARK_ROLE ).toBool () )
		{
			const QString text = model->pointer_for_index ( index ).to_string ();

			rows.append ( text.isEmpty () ? QStringLiteral ( "(root)" ) : text );
		}

		for ( int row = 0; row < model->rowCount ( index ); ++row )
		{
			pending.append ( model->index ( row, 0, index ) );
		}
	}

	rows.sort ();

	return rows;
}

QStringList TestJsonTreeModel::projection () const
{
	QStringList lines;

	append_projection ( root_index (), QString (), lines );

	return lines;
}

void TestJsonTreeModel::append_projection ( const QModelIndex& index, const QString& prefix, QStringList& outLines ) const
{
	if ( !index.isValid () )
	{
		return;
	}

	const QString label = model->data ( index, Qt::DisplayRole ).toString ();
	const QString path  = prefix + QStringLiteral ( "/" ) + label;

	outLines.append ( path );

	for ( int row = 0; row < model->rowCount ( index ); ++row )
	{
		append_projection ( model->index ( row, 0, index ), path, outLines );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Projection
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::empty_document_has_no_rows ()
{
	// No document, no file node -- the pane shows nothing rather than an empty placeholder row.

	QCOMPARE ( model->rowCount (), 0 );
	QVERIFY  ( !root_index ().isValid () );
	QVERIFY  ( !model->hasChildren ( QModelIndex () ) );
}

void TestJsonTreeModel::root_item_is_the_file ()
{
	// TREE-01: exactly one top-level row, standing for the FILE, with the root value's members beneath it.

	load ( SAMPLE_DOCUMENT );

	QCOMPARE ( model->rowCount (), 1 );

	const QModelIndex rootIndex = root_index ();

	QVERIFY  ( rootIndex.isValid () );
	QCOMPARE ( model->rowCount ( rootIndex ), 3 );

	// The file node and the root JSON Pointer name the same thing.

	QCOMPARE ( model->pointer_for_index ( rootIndex ).to_string (), QString () );
}

void TestJsonTreeModel::root_label_follows_the_file_path ()
{
	load ( SAMPLE_DOCUMENT );

	// Unsaved documents have no file name yet.

	QCOMPARE ( model->data ( root_index (), Qt::DisplayRole ).toString (), QStringLiteral ( "Untitled" ) );

	QSignalSpy dataChangedSpy ( model.get (), &QAbstractItemModel::dataChanged );

	document->set_file_path ( QStringLiteral ( "/tmp/example/test.json" ) );

	QCOMPARE ( model->data ( root_index (), Qt::DisplayRole ).toString (), QStringLiteral ( "test.json" ) );

	// The relabel has to be announced, or the pane keeps painting the old name after a Save As.

	QCOMPARE ( dataChangedSpy.count (), 1 );
}

void TestJsonTreeModel::object_members_show_keys ()
{
	load ( SAMPLE_DOCUMENT );

	const QStringList expected =
	{
		QStringLiteral ( "/Untitled" ),
		QStringLiteral ( "/Untitled/name" ),
		QStringLiteral ( "/Untitled/meta" ),
		QStringLiteral ( "/Untitled/meta/version" ),
		QStringLiteral ( "/Untitled/meta/stable" ),
		QStringLiteral ( "/Untitled/items" ),
		QStringLiteral ( "/Untitled/items/[0]" ),
		QStringLiteral ( "/Untitled/items/[1]" ),
		QStringLiteral ( "/Untitled/items/[2]" )
	};

	QCOMPARE ( projection (), expected );
}

void TestJsonTreeModel::array_elements_show_bracketed_indexes ()
{
	load ( R"([ "a", "b" ])" );

	const QModelIndex rootIndex = root_index ();

	QCOMPARE ( model->rowCount ( rootIndex ), 2 );
	QCOMPARE ( model->data ( model->index ( 0, 0, rootIndex ), Qt::DisplayRole ).toString (), QStringLiteral ( "[0]" ) );
	QCOMPARE ( model->data ( model->index ( 1, 0, rootIndex ), Qt::DisplayRole ).toString (), QStringLiteral ( "[1]" ) );
}

void TestJsonTreeModel::scalar_values_are_not_in_the_label ()
{
	// TREE-02: the tree is keys-only. "vje" is the value of /name and must not appear on the row.

	load ( SAMPLE_DOCUMENT );

	const QModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );

	QVERIFY  ( nameIndex.isValid () );
	QCOMPARE ( model->data ( nameIndex, Qt::DisplayRole ).toString (), QStringLiteral ( "name" ) );
}

void TestJsonTreeModel::scalar_values_are_in_the_accessible_name ()
{
	// ... but TREE-02 equally requires the value to reach a screen reader, which is the one place it does appear.

	load ( SAMPLE_DOCUMENT );

	const QModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );
	const QString     spoken    = model->data ( nameIndex, Qt::AccessibleTextRole ).toString ();

	QVERIFY2 ( spoken.contains ( QStringLiteral ( "name" ) ),   qPrintable ( spoken ) );
	QVERIFY2 ( spoken.contains ( QStringLiteral ( "string" ) ), qPrintable ( spoken ) );
	QVERIFY2 ( spoken.contains ( QStringLiteral ( "vje" ) ),    qPrintable ( spoken ) );

	// A container announces its size rather than a value.

	const QModelIndex itemsIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/items" ) ) );

	QVERIFY ( model->data ( itemsIndex, Qt::AccessibleTextRole ).toString ().contains ( QStringLiteral ( "3" ) ) );
}

void TestJsonTreeModel::type_icon_names_cover_every_kind ()
{
	// TREE-03. Asserted against the icon_names constants rather than string literals, so renaming an asset moves both
	// ends together; the point of the case is that all six kinds map, and map DISTINCTLY.

	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::Object ),  icon_names::TYPE_OBJECT );
	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::Array ),   icon_names::TYPE_ARRAY );
	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::String ),  icon_names::TYPE_STRING );
	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::Number ),  icon_names::TYPE_NUMBER );
	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::Boolean ), icon_names::TYPE_BOOLEAN );
	QCOMPARE ( JsonTreeModel::type_icon_name ( JsonKind::Null ),    icon_names::TYPE_NULL );

	QSet<QString> distinct;

	for ( JsonKind kind : { JsonKind::Object, JsonKind::Array, JsonKind::String,
	                        JsonKind::Number, JsonKind::Boolean, JsonKind::Null } )
	{
		distinct.insert ( JsonTreeModel::type_icon_name ( kind ) );
	}

	QCOMPARE ( distinct.size (), 6 );

	// The root file node is not one of the six -- it stands for the file, not for a JSON value (TREE-01).

	QVERIFY ( !distinct.contains ( icon_names::TYPE_DOCUMENT ) );
}

void TestJsonTreeModel::branches_report_children_without_materializing ()
{
	// TREE-08. hasChildren() drives the branch indicator on every visible row, so it must answer from the document. If
	// it populated instead, opening a large file would build the entire projection before the first paint.

	load ( SAMPLE_DOCUMENT );

	const QModelIndex metaIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) );
	const QModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );

	QVERIFY ( model->hasChildren ( metaIndex ) );
	QVERIFY ( !model->hasChildren ( nameIndex ) );

	// An empty container is a leaf as far as the indicator is concerned.

	load ( R"({ "empty": {} })" );

	QVERIFY ( !model->hasChildren ( model->index_for_pointer ( pointer ( QStringLiteral ( "/empty" ) ) ) ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Pointer correspondence
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::pointer_round_trips_through_index ()
{
	// Selection, Go To, Find, and the expansion restore all move between the two forms; a round trip that loses a token
	// would silently send the user to the wrong node.

	load ( SAMPLE_DOCUMENT );

	const QStringList paths =
	{
		QString (),
		QStringLiteral ( "/name" ),
		QStringLiteral ( "/meta" ),
		QStringLiteral ( "/meta/stable" ),
		QStringLiteral ( "/items" ),
		QStringLiteral ( "/items/2" )
	};

	for ( const QString& path : paths )
	{
		const QModelIndex index = model->index_for_pointer ( pointer ( path ) );

		QVERIFY2 ( index.isValid (), qPrintable ( path ) );
		QCOMPARE ( model->pointer_for_index ( index ).to_string (), path );
	}
}

void TestJsonTreeModel::index_for_pointer_materializes_ancestors ()
{
	// Go To must reach a node inside a branch the user has never opened, which means the lookup has to populate on the
	// way down rather than fail.

	load ( SAMPLE_DOCUMENT );

	const QModelIndex deepIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );

	QVERIFY  ( deepIndex.isValid () );
	QCOMPARE ( model->data ( deepIndex, Qt::DisplayRole ).toString (), QStringLiteral ( "version" ) );
}

void TestJsonTreeModel::index_for_pointer_rejects_an_unresolvable_pointer ()
{
	load ( SAMPLE_DOCUMENT );

	QVERIFY ( !model->index_for_pointer ( pointer ( QStringLiteral ( "/absent" ) ) ).isValid () );
	QVERIFY ( !model->index_for_pointer ( pointer ( QStringLiteral ( "/items/9" ) ) ).isValid () );
	QVERIFY ( !model->index_for_pointer ( pointer ( QStringLiteral ( "/name/deeper" ) ) ).isValid () );
}

void TestJsonTreeModel::nearest_index_falls_back_to_the_surviving_ancestor ()
{
	// NAV-03: after a whole-document commit reshapes the tree, the prior selection lands on the deepest surviving
	// ancestor -- never back at the root by default.

	load ( SAMPLE_DOCUMENT );

	const QModelIndex nearest = model->nearest_index_for_pointer ( pointer ( QStringLiteral ( "/meta/gone/deeper" ) ) );

	QVERIFY  ( nearest.isValid () );
	QCOMPARE ( model->pointer_for_index ( nearest ).to_string (), QStringLiteral ( "/meta" ) );

	// Nothing survives below the root -> the root.

	const QModelIndex fallback = model->nearest_index_for_pointer ( pointer ( QStringLiteral ( "/absent/deeper" ) ) );

	QVERIFY  ( fallback.isValid () );
	QCOMPARE ( model->pointer_for_index ( fallback ).to_string (), QString () );
}

//---------------------------------------------------------------------------------------------------------------------
// Incremental patching (TREE-07)
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::value_edit_emits_data_changed_only ()
{
	load ( SAMPLE_DOCUMENT );

	// Materialize the root's children first -- the model only patches what it has actually projected, and the pane
	// always has by this point, since it expands the file node on load.

	const QModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );

	QVERIFY ( nameIndex.isValid () );

	QSignalSpy dataChangedSpy  ( model.get (), &QAbstractItemModel::dataChanged );
	QSignalSpy modelResetSpy   ( model.get (), &QAbstractItemModel::modelReset );
	QSignalSpy rowsInsertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );
	QSignalSpy rowsRemovedSpy  ( model.get (), &QAbstractItemModel::rowsRemoved );

	QCOMPARE ( undo->set_string ( pointer ( QStringLiteral ( "/name" ) ), QStringLiteral ( "renamed" ) ), EditOutcome::Applied );

	// A scalar edit RELABELS one row and restructures nothing -- no insert, no removal, and certainly no reset. Its
	// ancestors are re-asked only for the change mark (TREE-10), which the edit has just given them.

	QCOMPARE ( rows_changed ( dataChangedSpy, Qt::DisplayRole ),               QStringList ( { "/name" } ) );
	QCOMPARE ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ), QStringList ( { "(root)", "/name" } ) );
	QCOMPARE ( modelResetSpy.count (),  0 );
	QCOMPARE ( rowsInsertedSpy.count (), 0 );
	QCOMPARE ( rowsRemovedSpy.count (),  0 );

	// The label is keys-only, so it is unchanged -- but the value the screen reader hears is not.

	QCOMPARE ( model->data ( nameIndex, Qt::DisplayRole ).toString (), QStringLiteral ( "name" ) );
	QVERIFY  ( model->data ( nameIndex, Qt::AccessibleTextRole ).toString ().contains ( QStringLiteral ( "renamed" ) ) );
}

void TestJsonTreeModel::rename_relabels_without_restructuring ()
{
	// EDIT-02. The member keeps its identity and its slot; only the label moves. A rename that arrived as a
	// remove-plus-insert would collapse the renamed branch and drop the selection.

	load ( SAMPLE_DOCUMENT );

	const QPersistentModelIndex metaIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) );

	QVERIFY ( metaIndex.isValid () );

	QSignalSpy rowsInsertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );
	QSignalSpy rowsRemovedSpy  ( model.get (), &QAbstractItemModel::rowsRemoved );
	QSignalSpy modelResetSpy   ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE ( undo->rename_key ( pointer ( QStringLiteral ( "/meta" ) ), QStringLiteral ( "metadata" ) ), EditOutcome::Applied );

	QCOMPARE ( rowsInsertedSpy.count (), 0 );
	QCOMPARE ( rowsRemovedSpy.count (),  0 );
	QCOMPARE ( modelResetSpy.count (),   0 );

	QVERIFY  ( metaIndex.isValid () );
	QCOMPARE ( model->data ( metaIndex, Qt::DisplayRole ).toString (), QStringLiteral ( "metadata" ) );

	// The renamed node's children came along with it.

	QCOMPARE ( model->rowCount ( metaIndex ), 2 );
}

void TestJsonTreeModel::add_child_inserts_one_row_and_keeps_identity ()
{
	load ( SAMPLE_DOCUMENT );

	// Materialize the root's children, then hold a persistent index on a sibling that must survive the insert.

	const QPersistentModelIndex itemsIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/items" ) ) );

	QVERIFY ( itemsIndex.isValid () );

	QSignalSpy rowsInsertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );
	QSignalSpy modelResetSpy   ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE
	(
		undo->add_child ( JsonPointer (), JsonKind::Boolean, QStringLiteral ( "active" ) ),
		EditOutcome::Applied
	);

	// Exactly one insertion of exactly one row, appended after the existing three -- not a reset.

	QCOMPARE ( rowsInsertedSpy.count (), 1 );
	QCOMPARE ( modelResetSpy.count (),   0 );

	const QList<QVariant> insertion = rowsInsertedSpy.first ();

	QCOMPARE ( insertion.at ( 1 ).toInt (), 3 );
	QCOMPARE ( insertion.at ( 2 ).toInt (), 3 );

	QCOMPARE ( model->rowCount ( root_index () ), 4 );

	// The untouched sibling is still the same row of the same tree.

	QVERIFY  ( itemsIndex.isValid () );
	QCOMPARE ( model->pointer_for_index ( itemsIndex ).to_string (), QStringLiteral ( "/items" ) );

	QCOMPARE
	(
		model->data ( model->index ( 3, 0, root_index () ), Qt::DisplayRole ).toString (),
		QStringLiteral ( "active" )
	);
}

void TestJsonTreeModel::delete_removes_one_row_and_keeps_identity ()
{
	load ( SAMPLE_DOCUMENT );

	const QPersistentModelIndex itemsIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/items" ) ) );
	const QPersistentModelIndex nameIndex  = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );

	QSignalSpy rowsRemovedSpy ( model.get (), &QAbstractItemModel::rowsRemoved );
	QSignalSpy modelResetSpy  ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/meta" ) ) ), EditOutcome::Applied );

	QCOMPARE ( rowsRemovedSpy.count (), 1 );
	QCOMPARE ( modelResetSpy.count (),  0 );

	const QList<QVariant> removal = rowsRemovedSpy.first ();

	QCOMPARE ( removal.at ( 1 ).toInt (), 1 );
	QCOMPARE ( removal.at ( 2 ).toInt (), 1 );

	QCOMPARE ( model->rowCount ( root_index () ), 2 );

	// The survivors keep their identity; /items simply shifted up a row.

	QVERIFY  ( nameIndex.isValid () );
	QVERIFY  ( itemsIndex.isValid () );
	QCOMPARE ( model->pointer_for_index ( itemsIndex ).to_string (), QStringLiteral ( "/items" ) );
	QCOMPARE ( itemsIndex.row (), 1 );
}

void TestJsonTreeModel::array_insert_relabels_the_following_siblings ()
{
	// Element labels are POSITIONAL, so inserting at the front renames every element after it even though none of them
	// changed identity. Forgetting the relabel leaves a tree reading [0] [1] [2] over four rows.

	load ( R"([ "a", "b", "c" ])" );

	const QPersistentModelIndex firstIndex = model->index ( 0, 0, root_index () );

	QCOMPARE ( model->data ( firstIndex, Qt::DisplayRole ).toString (), QStringLiteral ( "[0]" ) );

	// add_sibling places the new node immediately AFTER the target, so adding after [0] shifts the old [1] and [2].

	QCOMPARE ( undo->add_sibling ( pointer ( QStringLiteral ( "/0" ) ), JsonKind::Null ), EditOutcome::Applied );

	QCOMPARE ( model->rowCount ( root_index () ), 4 );

	const QStringList labels =
	{
		model->data ( model->index ( 0, 0, root_index () ), Qt::DisplayRole ).toString (),
		model->data ( model->index ( 1, 0, root_index () ), Qt::DisplayRole ).toString (),
		model->data ( model->index ( 2, 0, root_index () ), Qt::DisplayRole ).toString (),
		model->data ( model->index ( 3, 0, root_index () ), Qt::DisplayRole ).toString ()
	};

	QCOMPARE ( labels, QStringList ( { QStringLiteral ( "[0]" ), QStringLiteral ( "[1]" ),
	                                   QStringLiteral ( "[2]" ), QStringLiteral ( "[3]" ) } ) );

	// The original first element is untouched and still at row 0.

	QVERIFY  ( firstIndex.isValid () );
	QCOMPARE ( firstIndex.row (), 0 );
}

void TestJsonTreeModel::move_reorders_without_rebuilding ()
{
	// EDIT-08. A reorder must arrive as a move, not as a remove plus an insert -- only a move carries the row's
	// expansion state with it.

	load ( R"({ "first": { "x": 1 }, "second": 2 })" );

	const QPersistentModelIndex firstIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/first" ) ) );

	QVERIFY ( firstIndex.isValid () );

	QSignalSpy rowsMovedSpy    ( model.get (), &QAbstractItemModel::rowsMoved );
	QSignalSpy rowsRemovedSpy  ( model.get (), &QAbstractItemModel::rowsRemoved );
	QSignalSpy rowsInsertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );
	QSignalSpy modelResetSpy   ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE
	(
		undo->move_node ( pointer ( QStringLiteral ( "/first" ) ), MoveDirection::Down ),
		EditOutcome::Applied
	);

	QCOMPARE ( rowsMovedSpy.count (),    1 );
	QCOMPARE ( rowsRemovedSpy.count (),  0 );
	QCOMPARE ( rowsInsertedSpy.count (), 0 );
	QCOMPARE ( modelResetSpy.count (),   0 );

	QCOMPARE
	(
		model->data ( model->index ( 0, 0, root_index () ), Qt::DisplayRole ).toString (),
		QStringLiteral ( "second" )
	);

	// The moved node is the same item, now at row 1, still carrying its child.

	QVERIFY  ( firstIndex.isValid () );
	QCOMPARE ( firstIndex.row (), 1 );
	QCOMPARE ( model->rowCount ( firstIndex ), 1 );
}

void TestJsonTreeModel::change_of_type_replaces_the_subtree ()
{
	// EDIT-09. The node is swapped wholesale, so its old children go and the projection re-points at the new node --
	// without disturbing anything alongside it.

	load ( SAMPLE_DOCUMENT );

	const QPersistentModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );
	const QModelIndex           metaIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) );

	QCOMPARE ( model->rowCount ( metaIndex ), 2 );

	QSignalSpy rebuildSpy    ( model.get (), &JsonTreeModel::projection_about_to_rebuild );
	QSignalSpy rebuiltSpy    ( model.get (), &JsonTreeModel::projection_rebuilt );
	QSignalSpy modelResetSpy ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE
	(
		undo->change_type ( pointer ( QStringLiteral ( "/meta" ) ), JsonKind::String ),
		EditOutcome::Applied
	);

	// A local replacement, not a whole-model reset -- and bracketed so the pane can restore expansion around it.

	QCOMPARE ( modelResetSpy.count (), 0 );
	QCOMPARE ( rebuildSpy.count (),    1 );
	QCOMPARE ( rebuiltSpy.count (),    1 );

	const QModelIndex replacedIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) );

	QVERIFY  ( replacedIndex.isValid () );
	QCOMPARE ( model->rowCount ( replacedIndex ), 0 );
	QVERIFY  ( !model->hasChildren ( replacedIndex ) );

	// The sibling is untouched.

	QVERIFY  ( nameIndex.isValid () );
	QCOMPARE ( model->pointer_for_index ( nameIndex ).to_string (), QStringLiteral ( "/name" ) );
}

void TestJsonTreeModel::root_commit_rebuilds_and_announces_it ()
{
	// A Code View commit replaces the whole document. The projection cannot be patched -- but it must announce the
	// rebuild with the projection pair, which is what lets the pane put expansion and selection back (TREE-07).

	load ( SAMPLE_DOCUMENT );

	QSignalSpy rebuildSpy    ( model.get (), &JsonTreeModel::projection_about_to_rebuild );
	QSignalSpy rebuiltSpy    ( model.get (), &JsonTreeModel::projection_rebuilt );
	QSignalSpy modelResetSpy ( model.get (), &QAbstractItemModel::modelReset );

	QCOMPARE
	(
		undo->replace_subtree ( JsonPointer (), parse ( R"({ "name": "vje", "extra": true })" ), QStringLiteral ( "Commit" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( modelResetSpy.count (), 1 );
	QCOMPARE ( rebuildSpy.count (),    1 );
	QCOMPARE ( rebuiltSpy.count (),    1 );

	QCOMPARE ( model->rowCount ( root_index () ), 2 );

	// The surviving path is still addressable, which is what the pane's restore depends on.

	QVERIFY ( model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) ).isValid () );
}

void TestJsonTreeModel::unmaterialized_branch_is_not_patched ()
{
	// TREE-08's other half: an edit inside a branch the model has never projected must not force it into existence.
	// Nothing there is on screen, and materializing it would defeat the lazy population on exactly the large documents
	// it exists for.

	load ( SAMPLE_DOCUMENT );

	// Touch only the root's children, leaving /meta unmaterialized.

	QCOMPARE ( model->rowCount ( root_index () ), 3 );

	QSignalSpy rowsInsertedSpy ( model.get (), &QAbstractItemModel::rowsInserted );
	QSignalSpy dataChangedSpy  ( model.get (), &QAbstractItemModel::dataChanged );

	QCOMPARE
	(
		undo->add_child ( pointer ( QStringLiteral ( "/meta" ) ), JsonKind::Null, QStringLiteral ( "note" ) ),
		EditOutcome::Applied
	);

	// No rows are built for a branch that was never projected...

	QCOMPARE ( rowsInsertedSpy.count (), 0 );

	// ... but /meta is itself a visible row, and gaining its first child could mean gaining a branch indicator, so that
	// ONE row is relabelled. This is the line between "the node is projected" and "its children are". The change mark
	// is repainted on the projected rows it reached, and on nothing else.

	QCOMPARE ( rows_changed ( dataChangedSpy, Qt::DisplayRole ),               QStringList ( { "/meta" } ) );
	QCOMPARE ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ), QStringList ( { "(root)", "/meta" } ) );

	// A value edit deeper inside the unprojected branch relabels nothing -- there is no row for it. Only the projected
	// ancestors it marked are re-asked.

	dataChangedSpy.clear ();

	QCOMPARE
	(
		undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "2.0.1" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( rows_changed ( dataChangedSpy, Qt::DisplayRole ),               QStringList () );
	QCOMPARE ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ), QStringList ( { "(root)", "/meta" } ) );
	QCOMPARE ( rowsInsertedSpy.count (), 0 );

	// It is still correct on demand -- the branch is simply built from the current document when first expanded.

	QCOMPARE ( model->rowCount ( model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) ) ), 3 );
}

void TestJsonTreeModel::undo_restores_the_projection ()
{
	// Undo drives the same signals in reverse, so the projection has to survive a round trip -- the property test that
	// catches an asymmetric patch (redo o undo = identity).

	load ( SAMPLE_DOCUMENT );

	const QStringList before = projection ();

	QCOMPARE ( undo->add_child ( JsonPointer (), JsonKind::Array, QStringLiteral ( "tags" ) ), EditOutcome::Applied );
	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/meta" ) ) ),                   EditOutcome::Applied );
	QCOMPARE ( undo->rename_key ( pointer ( QStringLiteral ( "/name" ) ), QStringLiteral ( "title" ) ), EditOutcome::Applied );

	QVERIFY ( projection () != before );

	undo->undo ();
	undo->undo ();
	undo->undo ();

	QCOMPARE ( projection (), before );
}

//---------------------------------------------------------------------------------------------------------------------
// Document lifecycle
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::document_load_resets_the_model ()
{
	// A load is a different document. It goes through Qt's own reset and deliberately NOT the projection pair -- there
	// is no prior expansion worth restoring onto unrelated content.

	load ( SAMPLE_DOCUMENT );

	QSignalSpy modelResetSpy ( model.get (), &QAbstractItemModel::modelReset );
	QSignalSpy rebuildSpy    ( model.get (), &JsonTreeModel::projection_about_to_rebuild );

	load ( R"({ "other": 1 })" );

	QCOMPARE ( modelResetSpy.count (), 1 );
	QCOMPARE ( rebuildSpy.count (),    0 );

	QCOMPARE ( model->rowCount ( root_index () ), 1 );
	QCOMPARE
	(
		model->data ( model->index ( 0, 0, root_index () ), Qt::DisplayRole ).toString (),
		QStringLiteral ( "other" )
	);
}

//---------------------------------------------------------------------------------------------------------------------
// Drag and drop (EDIT-10)
//
//   The model DESCRIBES what may be dragged where and never performs the edit, so what is asserted here is exactly the
//   three questions Qt's drag machinery asks it -- and the answers are what withhold the drop indicator, which is how
//   an illegal drop becomes unreachable rather than reachable-and-rejected.
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::the_root_cannot_start_a_drag_and_a_container_can_receive_one ()
{
	load ( R"({"items":[10,20],"name":"x"})" );

	const QModelIndex root    = root_index ();
	const QModelIndex items   = model->index_for_pointer ( JsonPointer::parse ( "/items" ) );
	const QModelIndex element = model->index_for_pointer ( JsonPointer::parse ( "/items/0" ) );
	const QModelIndex scalar  = model->index_for_pointer ( JsonPointer::parse ( "/name" ) );

	// The document root has no siblings to reorder among -- the same guard delete, duplicate and move already apply.

	QVERIFY ( !( model->flags ( root ) & Qt::ItemIsDragEnabled ) );

	QVERIFY ( model->flags ( items )   & Qt::ItemIsDragEnabled );
	QVERIFY ( model->flags ( element ) & Qt::ItemIsDragEnabled );
	QVERIFY ( model->flags ( scalar )  & Qt::ItemIsDragEnabled );

	// A drop lands between a CONTAINER's children, so only a container receives one. A scalar row is drop-enabled
	// nowhere, which is what stops Qt offering a drop that would have to re-parent to mean anything.

	QVERIFY ( model->flags ( root )  & Qt::ItemIsDropEnabled );
	QVERIFY ( model->flags ( items ) & Qt::ItemIsDropEnabled );

	QVERIFY ( !( model->flags ( scalar )  & Qt::ItemIsDropEnabled ) );
	QVERIFY ( !( model->flags ( element ) & Qt::ItemIsDropEnabled ) );
}

void TestJsonTreeModel::a_drag_carries_its_rows_parent_and_nothing_else ()
{
	// The payload is the dragged rows' PARENT and nothing else: what is being dragged is the selection, which the
	// receiver already holds, and carrying a second copy of it would let the two disagree by the time the drop lands.

	load ( R"({"items":[10,20,30]})" );

	const QModelIndex first  = model->index_for_pointer ( JsonPointer::parse ( "/items/0" ) );
	const QModelIndex second = model->index_for_pointer ( JsonPointer::parse ( "/items/1" ) );

	std::unique_ptr<QMimeData> data ( model->mimeData ( { first, second } ) );

	QVERIFY  ( data != nullptr );
	QVERIFY  ( model->mimeTypes ().contains ( tree_drag_mime::VJE_TREE_ROWS ) );
	QCOMPARE ( QString::fromUtf8 ( data->data ( tree_drag_mime::VJE_TREE_ROWS ) ), QStringLiteral ( "/items" ) );

	QVERIFY ( model->mimeData ( {} ) == nullptr );
}

void TestJsonTreeModel::a_drop_is_accepted_only_between_its_own_parents_children ()
{
	load ( R"({"items":[10,20,30],"other":[1,2]})" );

	const QModelIndex items = model->index_for_pointer ( JsonPointer::parse ( "/items" ) );
	const QModelIndex other = model->index_for_pointer ( JsonPointer::parse ( "/other" ) );

	std::unique_ptr<QMimeData> data
	(
		model->mimeData ( { model->index_for_pointer ( JsonPointer::parse ( "/items/1" ) ) } )
	);

	// Between its own siblings, at every legal position including one past the end.

	QVERIFY ( model->canDropMimeData ( data.get (), Qt::CopyAction, 0, 0, items ) );
	QVERIFY ( model->canDropMimeData ( data.get (), Qt::CopyAction, 3, 0, items ) );

	// Among a different parent's children -- EDIT-10 reorders WITHIN a parent, and re-parenting raises the
	// key-collision and index questions nothing answers for moves.

	QVERIFY ( !model->canDropMimeData ( data.get (), Qt::CopyAction, 0, 0, other ) );

	// row == -1 is Qt's "ONTO this row", which is the band the pointer crosses in the middle of every row it passes.
	// Refusing it here rather than at the drop is what turns the indicator off over that band.

	QVERIFY ( !model->canDropMimeData ( data.get (), Qt::CopyAction, -1, 0, items ) );

	// And the viewport, which names no container at all.

	QVERIFY ( !model->canDropMimeData ( data.get (), Qt::CopyAction, 0, 0, QModelIndex () ) );
}

void TestJsonTreeModel::a_foreign_drag_is_refused_so_it_falls_through_to_the_window ()
{
	// FILE-09 opens a file dropped anywhere on the window. The tree now accepts drops, so it sits in front of that
	// handler -- and if it accepted a drag it cannot use, dropping a file on the tree would silently do nothing.

	load ( R"({"items":[10,20]})" );

	const QModelIndex items = model->index_for_pointer ( JsonPointer::parse ( "/items" ) );

	QMimeData files;

	files.setUrls ( { QUrl::fromLocalFile ( QStringLiteral ( "/tmp/example.json" ) ) } );

	QVERIFY ( !model->canDropMimeData ( &files,  Qt::CopyAction, 0, 0, items ) );
	QVERIFY ( !model->canDropMimeData ( nullptr, Qt::CopyAction, 0, 0, items ) );
}

void TestJsonTreeModel::move_action_is_not_offered_at_all ()
{
	// The load-bearing half of the arrangement with ReorderTreeView. QAbstractItemView removes the dragged rows
	// through the model when QDrag::exec returns Qt::MoveAction -- which would tear the nodes out from under the
	// undoable command that IS the reorder. Not advertising MoveAction makes that branch unreachable rather than
	// merely unused, so it cannot come back at a Qt upgrade.

	QVERIFY ( !( model->supportedDropActions () & Qt::MoveAction ) );
	QVERIFY (    model->supportedDropActions () & Qt::CopyAction );
}

QTEST_MAIN ( TestJsonTreeModel )

//---------------------------------------------------------------------------------------------------------------------
// Duplicate sibling keys (VAL-02 / FILE-04)
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::deleting_the_second_of_two_duplicate_keys_deletes_that_one ()
{
	// THE REPORTED DEFECT, reduced from the user's own document: `testObject` carrying `name` twice, an array of
	// objects and an array of scalars. Selecting the SECOND in the tree and deleting it removed the FIRST.
	//
	// The cause was not the delete: JsonPointer::resolve calls find_member, which returns the first match, so the
	// tree published the same pointer for both rows and EVERY pointer-routed command acted on the first. The delete
	// is simply where it was noticed.
	//
	// This drives the real route -- the tree's own pointer for the row, through the real UndoController -- because
	// the pointer's construction is exactly what was wrong. Asserting against a hand-built pointer would test the
	// assertion.

	load ( R"({"testObject":{"name":[{"a":1}],"name":[0,1,2]}})" );

	const QModelIndex object = model->index ( 0, 0, root_index () );   // testObject

	QCOMPARE ( model->rowCount ( object ), 2 );

	const QModelIndex firstArray  = model->index ( 0, 0, object );
	const QModelIndex secondArray = model->index ( 1, 0, object );

	// Both rows carry the same RFC 6901 text and are nevertheless different pointers.

	QCOMPARE ( model->pointer_for_index ( firstArray ).to_string (),
	           model->pointer_for_index ( secondArray ).to_string () );

	QVERIFY ( model->pointer_for_index ( firstArray ) != model->pointer_for_index ( secondArray ) );

	QCOMPARE ( undo->delete_node ( model->pointer_for_index ( secondArray ) ), EditOutcome::Applied );

	// The array of OBJECTS survives -- before this it was the one that went.

	QCOMPARE ( JsonSerializer::serialize ( *document->root () ), QStringLiteral ( R"({"testObject":{"name":[{"a":1}]}})" ) );

	// And the other way round, so the case cannot pass against a build that simply deletes the last member.

	load ( R"({"testObject":{"name":[{"a":1}],"name":[0,1,2]}})" );

	const QModelIndex reloaded = model->index ( 0, 0, root_index () );

	QCOMPARE ( undo->delete_node ( model->pointer_for_index ( model->index ( 0, 0, reloaded ) ) ),
	           EditOutcome::Applied );

	QCOMPARE ( JsonSerializer::serialize ( *document->root () ), QStringLiteral ( R"({"testObject":{"name":[0,1,2]}})" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Unsaved-change marks (TREE-10 / SET-14) and the tooltip (STYLE-17)
//---------------------------------------------------------------------------------------------------------------------

void TestJsonTreeModel::an_edit_marks_the_changed_row_and_its_ancestors ()
{
	load ( SAMPLE_DOCUMENT );

	model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );    // Project /meta's children.

	QCOMPARE ( marked_rows (), QStringList () );

	QCOMPARE ( undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) ), EditOutcome::Applied );

	// The default scope: the edited row and every row above it, and not its sibling /meta/stable nor /name beside it.

	QCOMPARE ( model->change_mark_scope (), config::tree::ChangeMarkScope::ChangedNodesAndAncestors );
	QCOMPARE ( marked_rows (), QStringList ( { "(root)", "/meta", "/meta/version" } ) );
}

void TestJsonTreeModel::the_file_node_only_scope_marks_the_root_alone ()
{
	load ( SAMPLE_DOCUMENT );

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::FileNodeOnly );

	model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );

	QCOMPARE ( marked_rows (), QStringList () );                               // Nothing changed, nothing marked.

	undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QCOMPARE ( marked_rows (), QStringList ( { "(root)" } ) );

	// And switching back shows the chain the edit had built -- the marks were kept all along.

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::ChangedNodesAndAncestors );

	QCOMPARE ( marked_rows (), QStringList ( { "(root)", "/meta", "/meta/version" } ) );
}

void TestJsonTreeModel::a_scope_change_repaints_every_projected_row ()
{
	load ( SAMPLE_DOCUMENT );

	model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );

	undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QSignalSpy dataChangedSpy ( model.get (), &QAbstractItemModel::dataChanged );

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::FileNodeOnly );

	// Every projected row, since any of them may have lost its dot -- and /items' elements, never projected, are not
	// among them.

	const QStringList everyProjectedRow = { "(root)", "/items", "/meta", "/meta/stable", "/meta/version", "/name" };

	QCOMPARE ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ), everyProjectedRow );

	// Setting the scope it already has says nothing.

	dataChangedSpy.clear ();

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::FileNodeOnly );

	QCOMPARE ( dataChangedSpy.count (), 0 );
}

void TestJsonTreeModel::a_clean_document_repaints_its_marks_away ()
{
	load ( SAMPLE_DOCUMENT );

	model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );

	undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QSignalSpy dataChangedSpy ( model.get (), &QAbstractItemModel::dataChanged );

	// What a save does to the document.

	document->set_dirty ( false );
	undo->set_clean ();

	QCOMPARE ( marked_rows (), QStringList () );

	QVERIFY ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ).contains ( QStringLiteral ( "/meta/version" ) ) );
	QVERIFY ( rows_changed ( dataChangedSpy, JsonTreeModel::CHANGE_MARK_ROLE ).contains ( QStringLiteral ( "(root)" ) ) );
}

void TestJsonTreeModel::the_tooltip_says_where_what_and_whether_changed ()
{
	load ( SAMPLE_DOCUMENT );

	const QModelIndex versionIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta/version" ) ) );
	const QModelIndex metaIndex    = model->index_for_pointer ( pointer ( QStringLiteral ( "/meta" ) ) );

	// A leaf: where it is, what type, and -- since TREE-02 keeps it out of the label -- its value, as JSON, so a string
	// is visibly a string. The label ("version") is NOT repeated: it is on screen.

	QCOMPARE
	(
		model->data ( versionIndex, Qt::ToolTipRole ).toString (),
		QStringLiteral ( "/meta/version\nstring\n\"2.0.0\"" )
	);

	// A container: where it is, and the status bar's own description of it.

	QCOMPARE
	(
		model->data ( metaIndex, Qt::ToolTipRole ).toString (),
		QStringLiteral ( "/meta\nobject \u00B7 2 members" )
	);

	// Changed: the dot is explained in words.

	undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QCOMPARE
	(
		model->data ( versionIndex, Qt::ToolTipRole ).toString (),
		QStringLiteral ( "/meta/version\nstring\n\"3\"\nUnsaved changes" )
	);

	// Under File node only the row carries no dot, so its tooltip does not claim one.

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::FileNodeOnly );

	QVERIFY ( !model->data ( versionIndex, Qt::ToolTipRole ).toString ().contains ( QStringLiteral ( "Unsaved" ) ) );
	QVERIFY (  model->data ( root_index (), Qt::ToolTipRole ).toString ().endsWith ( QStringLiteral ( "Unsaved changes" ) ) );
}

void TestJsonTreeModel::the_file_node_s_tooltip_is_its_full_path ()
{
	load ( SAMPLE_DOCUMENT );

	QCOMPARE ( model->data ( root_index (), Qt::ToolTipRole ).toString (), QStringLiteral ( "Untitled\nobject \u00B7 3 members" ) );

	// The label is the file NAME; the tooltip gives what the label cannot -- the folder it is in.

	const QString path = QStringLiteral ( "C:/data/projects/sample.json" );

	document->set_file_path ( path );

	QCOMPARE ( model->data ( root_index (), Qt::DisplayRole ).toString (), QStringLiteral ( "sample.json" ) );
	QCOMPARE ( model->data ( root_index (), Qt::ToolTipRole ).toString (), path + QStringLiteral ( "\nobject \u00B7 3 members" ) );
}

void TestJsonTreeModel::the_accessible_name_says_a_row_has_unsaved_changes ()
{
	load ( SAMPLE_DOCUMENT );

	const QModelIndex nameIndex = model->index_for_pointer ( pointer ( QStringLiteral ( "/name" ) ) );

	QVERIFY ( !model->data ( nameIndex, Qt::AccessibleTextRole ).toString ().contains ( QStringLiteral ( "unsaved" ) ) );

	undo->set_string ( pointer ( QStringLiteral ( "/name" ) ), QStringLiteral ( "renamed" ) );

	// NFR-05: the dot is a colour and a shape, which a screen reader cannot say. The name still comes first.

	const QString spoken = model->data ( nameIndex, Qt::AccessibleTextRole ).toString ();

	QVERIFY2 ( spoken.startsWith ( QStringLiteral ( "name, " ) ),           qPrintable ( spoken ) );
	QVERIFY2 ( spoken.endsWith   ( QStringLiteral ( ", unsaved changes" ) ), qPrintable ( spoken ) );
}

#include "tst_json_tree_model.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QAbstractItemModel>
#include <QSignalSpy>

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

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	QModelIndex root_index () const;

	// The projection rendered as "path -> label" lines, which makes a shape assertion readable as a whole rather than
	// as a dozen separate index lookups.

	QStringList projection () const;
	void        append_projection ( const QModelIndex& index, const QString& prefix, QStringList& outLines ) const;

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

	// A scalar edit touches one row and nothing else -- no insert, no removal, and certainly no reset.

	QCOMPARE ( dataChangedSpy.count (), 1 );
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
	// ONE row is refreshed. This is the line between "the node is projected" and "its children are".

	QCOMPARE ( dataChangedSpy.count (), 1 );

	// A value edit deeper inside the unprojected branch touches nothing at all -- there is no row to refresh.

	dataChangedSpy.clear ();

	QCOMPARE
	(
		undo->set_string ( pointer ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "2.0.1" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( dataChangedSpy.count (), 0 );

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

QTEST_MAIN ( TestJsonTreeModel )

#include "tst_json_tree_model.moc"

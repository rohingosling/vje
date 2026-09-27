//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <vje_core/services/JsonSerializer.hpp>
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

	void scalars_and_nulls_are_editable_but_containers_drill_in ();
	void a_null_field_takes_a_typed_entry_as_a_json_literal ();
	void a_null_field_typed_entry_falls_back_to_a_string ();

	// The provisional row (EDITOR-15).

	void an_empty_object_presents_with_a_provisional_row ();
	void a_provisional_row_is_view_only_until_its_key_is_committed ();
	void committing_a_key_creates_the_member_with_null ();
	void an_empty_or_duplicate_key_creates_nothing ();
	void switching_key_editing_off_withdraws_the_provisional_row ();
	void every_key_is_renameable_whatever_its_value ();
	void a_rename_reaches_the_document_through_undo ();
	void a_rename_onto_an_existing_key_is_refused ();
	void a_key_cell_names_its_rivals ();
	void switching_key_editing_off_closes_both_rename_routes ();
	void a_commit_reaches_the_document_through_undo ();
	void an_invalid_number_is_refused ();
	void each_duplicate_key_is_editable_and_commits_to_its_own_member ();
	void the_setting_clears_the_rival_keys_the_editor_refuses ();

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

// EDITOR-13 (Phase 15, closing OQ-2) WIDENED THIS, and the case is renamed with it -- it used to be called
// only_scalar_values_are_editable, which is the rule that changed. A null field now opens an editor and takes a typed
// entry, exactly as the array table's null cells have since Phase 9; only a CONTAINER stays non-editable, because
// activating one drills in (EDITOR-05) rather than editing.

void TestJsonFormModel::scalars_and_nulls_are_editable_but_containers_drill_in ()
{
	model->present ( JsonPointer () );

	QVERIFY ( model->flags ( model->index ( 0, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( model->flags ( model->index ( 2, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	// lastLogin is null -- editable as of EDITOR-13, and the whole point of the change.

	QVERIFY ( model->flags ( model->index ( 3, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	// roles is an array and row 5 an object: both drill in, so neither opens an editor.

	QVERIFY ( !model->flags ( model->index ( 4, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( 5, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
}

// EDITOR-13's commit rule, which is EDITOR-12's verbatim: the text is read as a JSON LITERAL, so a null field's new
// KIND comes from what was typed. Asserted through the model's own setData, since that is the path the delegate takes.
//
// "lastLogin" is the null member of the sample document; row 3.

void TestJsonFormModel::a_null_field_takes_a_typed_entry_as_a_json_literal ()
{
	model->present ( JsonPointer () );

	const QModelIndex nullField = model->index ( 3, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( model->setData ( nullField, QStringLiteral ( "42" ) ) );

	const JsonNode* const committed = document->resolve ( pointer ( QStringLiteral ( "/lastLogin" ) ) );

	QVERIFY  ( committed != nullptr );
	QCOMPARE ( committed->kind (), JsonKind::Number );

	// One undo step, and it restores the null rather than removing the member.

	QVERIFY ( undo->can_undo () );

	undo->undo ();

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/lastLogin" ) ) )->kind (), JsonKind::Null );
}

// The other arm of the literal rule: text that is not a JSON literal commits as a string. Without this, the case above
// would pass against an implementation that only ever parsed numbers.

void TestJsonFormModel::a_null_field_typed_entry_falls_back_to_a_string ()
{
	model->present ( JsonPointer () );

	QVERIFY ( model->setData ( model->index ( 3, JsonFormModel::VALUE_COLUMN ), QStringLiteral ( "yesterday" ) ) );

	const JsonNode* const committed = document->resolve ( pointer ( QStringLiteral ( "/lastLogin" ) ) );

	QVERIFY  ( committed != nullptr );
	QCOMPARE ( committed->kind (), JsonKind::String );
	QCOMPARE ( committed->string_value (), QStringLiteral ( "yesterday" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The provisional row (EDITOR-15)
//
// The counterpart of EDITOR-12's, differing in the one way that matters: an array element needs only a VALUE, while a
// member needs a KEY. So it materializes on the key, creating the member with null, and the value follows as an
// ordinary EDITOR-13 typed entry -- two steps, two undo entries, no two-cell staging anywhere.
//---------------------------------------------------------------------------------------------------------------------

void TestJsonFormModel::an_empty_object_presents_with_a_provisional_row ()
{
	load ( R"({ "empty": {} })" );

	model->present ( pointer ( QStringLiteral ( "/empty" ) ) );

	QVERIFY  ( model->has_provisional_row () );
	QCOMPARE ( model->member_count (), 0 );
	QCOMPARE ( model->rowCount (), 1 );          // The provisional row alone.

	QVERIFY ( model->is_provisional_row ( 0 ) );
}

// VIEW-ONLY is the load-bearing half: until the key is committed the document has not been touched, so a user who
// reaches the row and changes their mind leaves no dirty flag and no undo step behind.

void TestJsonFormModel::a_provisional_row_is_view_only_until_its_key_is_committed ()
{
	model->present ( JsonPointer () );

	const int members = model->member_count ();

	model->set_provisional_row ( true );

	QCOMPARE ( model->rowCount (), members + 1 );

	QVERIFY ( !document->is_dirty () );
	QVERIFY ( !undo->can_undo () );

	// Only the KEY cell edits: the value cell has no member to give a value to yet.

	QVERIFY (  model->flags ( model->index ( members, JsonFormModel::KEY_COLUMN   ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( !model->flags ( model->index ( members, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	model->set_provisional_row ( false );

	QCOMPARE ( model->rowCount (), members );
	QVERIFY  ( !document->is_dirty () );
}

void TestJsonFormModel::committing_a_key_creates_the_member_with_null ()
{
	model->present ( JsonPointer () );

	model->set_provisional_row ( true );

	QVERIFY ( model->materialize_provisional ( QStringLiteral ( "nickname" ) ) );

	const JsonNode* const created = document->resolve ( pointer ( QStringLiteral ( "/nickname" ) ) );

	QVERIFY  ( created != nullptr );
	QCOMPARE ( created->kind (), JsonKind::Null );      // Null, so EDITOR-13's typed entry is the second step.

	// ONE undo step, and the provisional row is gone -- the real member stands in its place.

	QVERIFY  ( undo->can_undo () );
	QVERIFY  ( !model->has_provisional_row () );

	undo->undo ();

	QVERIFY ( document->resolve ( pointer ( QStringLiteral ( "/nickname" ) ) ) == nullptr );
}

// Both refusals leave the document untouched. The empty key is the one a stray Enter on an untouched field produces,
// and "" is a legal JSON key -- so it has to be refused deliberately rather than by accident.

void TestJsonFormModel::an_empty_or_duplicate_key_creates_nothing ()
{
	model->present ( JsonPointer () );

	model->set_provisional_row ( true );

	QVERIFY ( !model->materialize_provisional ( QString () ) );
	QVERIFY ( !model->materialize_provisional ( QStringLiteral ( "name" ) ) );   // Already a member (VAL-02).

	QVERIFY ( !document->is_dirty () );
	QVERIFY ( !undo->can_undo () );
}

// SET-05a gates it, because the row's FIRST step is impossible without key editing -- offering a row nothing can be
// typed into would read as a broken control.

void TestJsonFormModel::switching_key_editing_off_withdraws_the_provisional_row ()
{
	model->set_key_editing_allowed ( false );

	load ( R"({ "empty": {} })" );

	model->present ( pointer ( QStringLiteral ( "/empty" ) ) );

	QVERIFY  ( !model->has_provisional_row () );
	QCOMPARE ( model->rowCount (), 0 );

	model->set_provisional_row ( true );

	QVERIFY ( !model->has_provisional_row () );
}

// The key column asks a DIFFERENT question from the value column, and the difference is still the point -- narrowed by
// EDITOR-13 to containers, whose value drills in while the key naming it renames perfectly well (EDIT-02).

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

	// roles is an array -- its value drills in rather than editing, and its key renames.

	QVERIFY ( !model->flags ( model->index ( 4, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
}

void TestJsonFormModel::switching_key_editing_off_closes_both_rename_routes ()
{
	// SET-05 / EDIT-02. The point of the case is the SECOND assertion set: a guard that only cleared ItemIsEditable
	// would leave setData writing happily, so the setting would decorate the UI while the write path walked past it --
	// which is exactly the trap is_editable_row's comment warns about.

	model->present ( JsonPointer () );

	const QString originalKey = model->key_for_row ( 1 );

	QVERIFY ( model->flags ( model->index ( 1, JsonFormModel::KEY_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	QSignalSpy keyColumnReflagged ( model.get (), &QAbstractItemModel::dataChanged );

	model->set_key_editing_allowed ( false );

	// Route 1: the cell no longer opens.

	for ( int row = 0; row < model->rowCount (); ++row )
	{
		QVERIFY2
		(
			!model->flags ( model->index ( row, JsonFormModel::KEY_COLUMN ) ).testFlag ( Qt::ItemIsEditable ),
			qPrintable ( QStringLiteral ( "row %1 (%2) should not be renameable" )
			             .arg ( row ).arg ( model->key_for_row ( row ) ) )
		);
	}

	// Route 2: and neither does the commit path, whatever asked it.

	QVERIFY ( !model->setData ( model->index ( 1, JsonFormModel::KEY_COLUMN ),
	                            QStringLiteral ( "fullName" ), Qt::EditRole ) );

	QCOMPARE ( model->key_for_row ( 1 ), originalKey );

	// The VALUE column is untouched -- this setting is about keys, not about making the form read-only.

	QVERIFY ( model->flags ( model->index ( 1, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	// An open form reflects the change at once rather than at the next presentation, and does so with a dataChanged
	// over the key column -- not a reset, which would cost the widths, the scroll and the current cell (EDITOR-03).

	QCOMPARE ( keyColumnReflagged.count (), 1 );

	const QModelIndex topLeft     = keyColumnReflagged.first ().at ( 0 ).toModelIndex ();
	const QModelIndex bottomRight = keyColumnReflagged.first ().at ( 1 ).toModelIndex ();

	QCOMPARE ( topLeft.column (),     static_cast<int> ( JsonFormModel::KEY_COLUMN ) );
	QCOMPARE ( bottomRight.column (), static_cast<int> ( JsonFormModel::KEY_COLUMN ) );
	QCOMPARE ( topLeft.row (),        0 );
	QCOMPARE ( bottomRight.row (),    model->rowCount () - 1 );

	// Switching it back on restores both routes.

	model->set_key_editing_allowed ( true );

	QVERIFY ( model->flags ( model->index ( 1, JsonFormModel::KEY_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( model->setData ( model->index ( 1, JsonFormModel::KEY_COLUMN ),
	                           QStringLiteral ( "fullName" ), Qt::EditRole ) );
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

void TestJsonFormModel::each_duplicate_key_is_editable_and_commits_to_its_own_member ()
{
	// RENAMED AND REWRITTEN FOR THE RULE THAT CHANGED, not deleted. This case was
	// duplicate_keys_are_presented_but_read_only, and it asserted the Phase 7 rule: both duplicates shown, neither
	// editable, because a JSON Pointer resolves to the FIRST match and a commit against the second would silently
	// write the first.
	//
	// That reason is gone. pointer_for_row records WHICH member it means (15h.5), so a commit lands where the user is
	// looking -- and the rule lifts for EVERY document rather than answering to SET-03a, since a file that arrived
	// with duplicates was never the user's doing.

	load ( R"({ "name": "first", "name": "second", "other": 1 })" );

	model->present ( JsonPointer () );

	QCOMPARE ( model->rowCount (), 3 );

	QCOMPARE ( model->index ( 0, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "first" ) );
	QCOMPARE ( model->index ( 1, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "second" ) );

	QVERIFY ( model->flags ( model->index ( 0, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( model->flags ( model->index ( 1, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( model->flags ( model->index ( 2, JsonFormModel::VALUE_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );

	// THE CLAIM THAT MATTERS, and the one the old rule existed to avoid needing: a commit on the SECOND row writes
	// the second member. Against a build that resolves by first match this writes "first" and the case fails on the
	// row it did not touch, which is exactly the silent corruption the read-only rule was protecting against.

	QVERIFY ( model->setData ( model->index ( 1, JsonFormModel::VALUE_COLUMN ),
	                           QStringLiteral ( "changed" ), Qt::EditRole ) );

	QCOMPARE ( model->index ( 0, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "first" ) );
	QCOMPARE ( model->index ( 1, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "changed" ) );

	// And the two rows carry different pointers with identical RFC 6901 text -- the ambiguity is the notation's, and
	// the disambiguation rides alongside it rather than in it.

	QCOMPARE ( model->pointer_for_row ( 0 ).to_string (), model->pointer_for_row ( 1 ).to_string () );
	QVERIFY  ( model->pointer_for_row ( 0 ) != model->pointer_for_row ( 1 ) );

	// The reverse map answers with the row the pointer names, not the first one that matches its text.

	QCOMPARE ( model->row_for_pointer ( model->pointer_for_row ( 1 ) ), 1 );
	QCOMPARE ( model->row_for_pointer ( model->pointer_for_row ( 0 ) ), 0 );

	// The KEY is renameable on a duplicate as well, and the rename lands on the member the row names. This was the
	// second copy of the Phase 7 guard -- the value half lifted in 15h.5, the key half did not -- found by the
	// MainWindow harness together with Rename Key's copy. Against the guarded build the flag is missing and the rename
	// refused; against a first-match build the FIRST row is renamed.

	QVERIFY ( model->flags ( model->index ( 1, JsonFormModel::KEY_COLUMN ) ).testFlag ( Qt::ItemIsEditable ) );
	QVERIFY ( model->rename_row ( 1, QStringLiteral ( "renamed" ) ) );

	QCOMPARE ( model->index ( 0, JsonFormModel::KEY_COLUMN ).data ().toString (),   QStringLiteral ( "name" ) );
	QCOMPARE ( model->index ( 1, JsonFormModel::KEY_COLUMN ).data ().toString (),   QStringLiteral ( "renamed" ) );
	QCOMPARE ( model->index ( 1, JsonFormModel::VALUE_COLUMN ).data ().toString (), QStringLiteral ( "changed" ) );
}

void TestJsonFormModel::the_setting_clears_the_rival_keys_the_editor_refuses ()
{
	// THE REPORTED SYMPTOM (2026-08-28): with Settings > General > Allow duplicate keys set to Yes, a duplicate key
	// still could not be created.
	//
	// The rival-key list is why, and it is worth naming because it is nowhere near UndoController, which the setting
	// had been wired to. JsonKeyValidator refuses a rival as QValidator::Intermediate, and QStyledItemDelegate
	// honours that by refusing to CLOSE the editor -- so the key could not be typed at all, however permissive the
	// command layer had become. Three separate guards sat above the policy; this is the one with no message.
	//
	// Written as an opposing pair, because a list that was always empty would satisfy the second half alone.

	load ( R"({ "a": 1, "b": 2 })" );

	model->present ( JsonPointer () );

	const QModelIndex firstKey = model->index ( 0, JsonFormModel::KEY_COLUMN );

	QCOMPARE ( model->data ( firstKey, cell_roles::RIVAL_KEYS ).toStringList (), QStringList { QStringLiteral ( "b" ) } );

	model->set_duplicate_keys_allowed ( true );

	QVERIFY ( model->data ( firstKey, cell_roles::RIVAL_KEYS ).toStringList ().isEmpty () );

	// And the rename itself lands, which is the half the command layer owns -- UndoController's own guard answers to
	// the same setting, pushed in separately (SET-03a).

	undo->set_allow_duplicate_keys ( true );

	QVERIFY ( model->rename_row ( 0, QStringLiteral ( "b" ) ) );

	QCOMPARE ( JsonSerializer::serialize ( *document->root () ), QStringLiteral ( R"({"b":1,"b":2})" ) );
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

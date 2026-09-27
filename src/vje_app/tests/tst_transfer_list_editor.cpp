//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for TransferListEditor -- the SET-04 control: two lists and a set of buttons producing an ordered subset
//   of a fixed option set.
//
//   IT IS DRIVEN AS A WIDGET rather than through a pure helper, because there is no pure helper to drive: the result IS
//   the left list's contents, and the rules worth pinning are all about what the two lists hold after a gesture. That
//   is fine offscreen -- selection, insertion, and ordering are all model state, and none of it needs a display server
//   or keyboard focus (the offscreen platform rules out only the focus-gated claims, and there are none here).
//
//   The two rules that carry the most weight are the ones a reasonable implementation gets wrong:
//
//     - The REPEATABLE option is copied, not moved, and deleted rather than returned. Every other option is unique.
//     - A removed option goes back at its OPTION-ORDER position, not the end -- so the available list does not depend
//       on the history of the session.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/TransferListEditor.hpp"

#include <QtTest/QtTest>

#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>

#include <memory>

using namespace vje;

class TestTransferListEditor : public QObject
{
	Q_OBJECT

private slots:

	void init ();

	void it_opens_with_the_chosen_list_in_order_and_the_rest_available ();
	void adding_moves_an_option_across_and_inserts_after_the_current_row ();
	void the_repeatable_option_is_copied_rather_than_moved ();
	void removing_returns_an_option_to_its_option_order_position ();
	void removing_the_repeatable_option_deletes_it ();
	void reordering_moves_one_place_and_keeps_the_selection ();
	void restore_defaults_returns_the_shipped_result ();
	void a_stored_result_naming_an_unknown_option_still_opens ();
	void every_change_is_reported_once_it_has_happened ();
	void the_buttons_are_disabled_rather_than_dead ();
	void the_four_arrows_are_one_set_of_the_same_size ();

private:

	// How much ink a button's arrow puts on the page: the summed darkness of every pixel the button paints beyond its
	// own chrome, read back from a real render. A COUNT of dark pixels would be too coarse -- antialiasing spreads a
	// small triangle's edge over a ring of partial pixels, and the whole point of the case is a shape comparison.

	static qreal arrow_ink ( QPushButton* button );

	// Five ordinary options plus the repeatable one at the head, which is the toolbar's shape exactly.

	SettingsField field ();

	static QStringList contents ( const QListWidget* list );

	std::unique_ptr<TransferListEditor> build ( const QStringList& chosen );

	QListWidget* chosen_list    ( TransferListEditor* editor ) const;
	QListWidget* available_list ( TransferListEditor* editor ) const;
	QPushButton* button         ( TransferListEditor* editor, const QString& name ) const;

	static const QString BAR;
};

const QString TestTransferListEditor::BAR = QStringLiteral ( "sep" );

SettingsField TestTransferListEditor::field ()
{
	SettingsField built;

	built.kind               = SettingsFieldKind::TransferList;
	built.key                = QStringLiteral ( "test.layout" );
	built.chosenListLabel    = QStringLiteral ( "Chosen" );
	built.availableListLabel = QStringLiteral ( "Available" );
	built.repeatableValue    = BAR;

	built.options =
	{
		{ QStringLiteral ( "--- Separator ---" ), BAR },
		{ QStringLiteral ( "Alpha" ),   QStringLiteral ( "a" ) },
		{ QStringLiteral ( "Bravo" ),   QStringLiteral ( "b" ) },
		{ QStringLiteral ( "Charlie" ), QStringLiteral ( "c" ) },
		{ QStringLiteral ( "Delta" ),   QStringLiteral ( "d" ) },
		{ QStringLiteral ( "Echo" ),    QStringLiteral ( "e" ) }
	};

	built.defaultValue = QStringList { QStringLiteral ( "a" ), BAR, QStringLiteral ( "b" ) };

	return built;
}

QStringList TestTransferListEditor::contents ( const QListWidget* list )
{
	QStringList values;

	for ( int row = 0; row < list->count (); ++row )
	{
		values.append ( list->item ( row )->data ( Qt::UserRole ).toString () );
	}

	return values;
}

std::unique_ptr<TransferListEditor> TestTransferListEditor::build ( const QStringList& chosen )
{
	return std::make_unique<TransferListEditor> ( field (), chosen );
}

QListWidget* TestTransferListEditor::chosen_list ( TransferListEditor* editor ) const
{
	return editor->findChild<QListWidget*> ( object_names::CHOSEN_LIST );
}

QListWidget* TestTransferListEditor::available_list ( TransferListEditor* editor ) const
{
	return editor->findChild<QListWidget*> ( object_names::AVAILABLE_LIST );
}

QPushButton* TestTransferListEditor::button ( TransferListEditor* editor, const QString& name ) const
{
	return editor->findChild<QPushButton*> ( name );
}

qreal TestTransferListEditor::arrow_ink ( QPushButton* button )
{
	const QImage rendered = button->grab ().toImage ();

	qreal total = 0.0;

	for ( int y = 0; y < rendered.height (); ++y )
	{
		for ( int x = 0; x < rendered.width (); ++x )
		{
			total += ( 255.0 - qGray ( rendered.pixel ( x, y ) ) );
		}
	}

	return total;
}

void TestTransferListEditor::init ()
{
	// Nothing to reset: every case builds its own editor over a fresh field.
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestTransferListEditor::it_opens_with_the_chosen_list_in_order_and_the_rest_available ()
{
	const auto editor = build ( { QStringLiteral ( "c" ), BAR, QStringLiteral ( "a" ) } );

	// The chosen list is the RESULT's order, not the option order.

	QCOMPARE ( contents ( chosen_list ( editor.get () ) ), QStringList ( { "c", BAR, "a" } ) );
	QCOMPARE ( editor->chosen (), QStringList ( { "c", BAR, "a" } ) );

	// The available list is what is left, in OPTION order -- and the repeatable entry stays in it even though the
	// result already uses one.

	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "b", "d", "e" } ) );
}

void TestTransferListEditor::adding_moves_an_option_across_and_inserts_after_the_current_row ()
{
	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ) } );

	// Inserted AFTER the chosen list's current row, so repeated adds build a run in order rather than needing to be
	// reordered afterwards.

	chosen_list ( editor.get () )->setCurrentRow ( 0 );
	available_list ( editor.get () )->setCurrentRow ( available_list ( editor.get () )->count () - 1 );   // "e"

	button ( editor.get (), object_names::ADD_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "e", "b" } ) );

	// The new row is the selection, so the next add lands after it.

	QCOMPARE ( chosen_list ( editor.get () )->currentRow (), 1 );

	// And it is gone from the available list: an ordinary option is MOVED.

	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "c", "d" } ) );

	// With nothing selected on the left, an add appends rather than refusing.

	chosen_list ( editor.get () )->setCurrentRow ( -1 );
	available_list ( editor.get () )->setCurrentRow ( 1 );                                                // "c"

	button ( editor.get (), object_names::ADD_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "e", "b", "c" } ) );
}

void TestTransferListEditor::the_repeatable_option_is_copied_rather_than_moved ()
{
	const auto editor = build ( { QStringLiteral ( "a" ) } );

	chosen_list ( editor.get () )->setCurrentRow ( 0 );
	available_list ( editor.get () )->setCurrentRow ( 0 );                                                // the separator

	button ( editor.get (), object_names::ADD_BUTTON )->click ();
	button ( editor.get (), object_names::ADD_BUTTON )->click ();

	// Twice, because it is a structural mark rather than a thing there is one of -- and it is still on offer.

	QCOMPARE ( editor->chosen (), QStringList ( { "a", BAR, BAR } ) );
	QVERIFY ( contents ( available_list ( editor.get () ) ).contains ( BAR ) );
}

void TestTransferListEditor::removing_returns_an_option_to_its_option_order_position ()
{
	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ), QStringLiteral ( "c" ) } );

	// Available is { sep, d, e }. Removing "b" must land it between the separator and "d" -- its OPTION-ORDER slot --
	// rather than at the end, or removing and re-adding twice would leave the list quietly rearranged.

	chosen_list ( editor.get () )->setCurrentRow ( 1 );

	button ( editor.get (), object_names::REMOVE_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "c" } ) );
	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "b", "d", "e" } ) );

	// And again with the first option, which has to go ahead of "b".

	chosen_list ( editor.get () )->setCurrentRow ( 0 );

	button ( editor.get (), object_names::REMOVE_BUTTON )->click ();

	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "a", "b", "d", "e" } ) );
}

void TestTransferListEditor::removing_the_repeatable_option_deletes_it ()
{
	const auto editor = build ( { QStringLiteral ( "a" ), BAR, BAR } );

	QCOMPARE ( contents ( available_list ( editor.get () ) ).count ( BAR ), 1 );

	chosen_list ( editor.get () )->setCurrentRow ( 1 );

	button ( editor.get (), object_names::REMOVE_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", BAR } ) );

	// It never left the available list, so it does not come back to it -- exactly one entry, not two.

	QCOMPARE ( contents ( available_list ( editor.get () ) ).count ( BAR ), 1 );
}

void TestTransferListEditor::reordering_moves_one_place_and_keeps_the_selection ()
{
	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ), QStringLiteral ( "c" ) } );

	chosen_list ( editor.get () )->setCurrentRow ( 2 );

	button ( editor.get (), object_names::MOVE_UP_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "c", "b" } ) );
	QCOMPARE ( chosen_list ( editor.get () )->currentRow (), 1 );

	button ( editor.get (), object_names::MOVE_DOWN_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "b", "c" } ) );
	QCOMPARE ( chosen_list ( editor.get () )->currentRow (), 2 );
}

void TestTransferListEditor::restore_defaults_returns_the_shipped_result ()
{
	const auto editor = build ( { QStringLiteral ( "e" ), QStringLiteral ( "d" ) } );

	button ( editor.get (), object_names::RESTORE_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), field ().defaultValue.toStringList () );

	// Both lists are rebuilt, not just the left one -- the options the old result held are back on offer.

	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "c", "d", "e" } ) );
}

void TestTransferListEditor::a_stored_result_naming_an_unknown_option_still_opens ()
{
	// Tolerant rather than strict: an option this build has not got, and a repeat of a unique one, are both skipped so
	// the page always opens (a settings file from a newer build, or one hand-edited).

	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "zzz" ), QStringLiteral ( "a" ), QStringLiteral ( "b" ) } );

	QCOMPARE ( editor->chosen (), QStringList ( { "a", "b" } ) );
	QCOMPARE ( contents ( available_list ( editor.get () ) ), QStringList ( { BAR, "c", "d", "e" } ) );
}

void TestTransferListEditor::every_change_is_reported_once_it_has_happened ()
{
	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ) } );

	QSignalSpy spy ( editor.get (), &TransferListEditor::chosen_changed );

	chosen_list ( editor.get () )->setCurrentRow ( 1 );

	button ( editor.get (), object_names::MOVE_UP_BUTTON )->click ();

	// One gesture may report more than once -- a reorder is a take followed by an insert, and both reach the model that
	// reporting rides on (which is what makes a drag-reorder report itself at all). What is guaranteed is that the LAST
	// report carries the settled result, and that is all the dialog needs: its snapshot is written, not read, until OK.

	QVERIFY ( spy.count () > 0 );
	QCOMPARE ( spy.takeLast ().at ( 0 ).toStringList (), QStringList ( { "b", "a" } ) );

	// A gesture that changes nothing reports nothing: Move Up on the first row is refused rather than round-tripped.

	spy.clear ();

	chosen_list ( editor.get () )->setCurrentRow ( 0 );

	button ( editor.get (), object_names::MOVE_UP_BUTTON )->click ();

	QCOMPARE ( spy.count (), 0 );
}

void TestTransferListEditor::the_buttons_are_disabled_rather_than_dead ()
{
	// Disabled-not-hidden, the rule the menus and the rest of the dialog follow.

	const auto editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ) } );

	chosen_list ( editor.get () )->setCurrentRow ( -1 );
	available_list ( editor.get () )->setCurrentRow ( -1 );

	QVERIFY ( !button ( editor.get (), object_names::ADD_BUTTON )->isEnabled () );
	QVERIFY ( !button ( editor.get (), object_names::REMOVE_BUTTON )->isEnabled () );
	QVERIFY ( !button ( editor.get (), object_names::MOVE_UP_BUTTON )->isEnabled () );
	QVERIFY ( !button ( editor.get (), object_names::MOVE_DOWN_BUTTON )->isEnabled () );

	// The first row cannot move up and the last cannot move down.

	chosen_list ( editor.get () )->setCurrentRow ( 0 );

	QVERIFY ( button ( editor.get (), object_names::REMOVE_BUTTON )->isEnabled () );
	QVERIFY ( !button ( editor.get (), object_names::MOVE_UP_BUTTON )->isEnabled () );
	QVERIFY ( button ( editor.get (), object_names::MOVE_DOWN_BUTTON )->isEnabled () );

	chosen_list ( editor.get () )->setCurrentRow ( 1 );

	QVERIFY ( button ( editor.get (), object_names::MOVE_UP_BUTTON )->isEnabled () );
	QVERIFY ( !button ( editor.get (), object_names::MOVE_DOWN_BUTTON )->isEnabled () );

	available_list ( editor.get () )->setCurrentRow ( 0 );

	QVERIFY ( button ( editor.get (), object_names::ADD_BUTTON )->isEnabled () );

	// Restore Defaults reports that there is nothing to restore, rather than sitting enabled and doing nothing.

	QVERIFY ( button ( editor.get (), object_names::RESTORE_BUTTON )->isEnabled () );

	button ( editor.get (), object_names::RESTORE_BUTTON )->click ();

	QCOMPARE ( editor->chosen (), field ().defaultValue.toStringList () );
	QVERIFY ( !button ( editor.get (), object_names::RESTORE_BUTTON )->isEnabled () );
}

void TestTransferListEditor::the_four_arrows_are_one_set_of_the_same_size ()
{
	// The arrows were four TEXT glyphs (U+25C0 / U+25B6 / U+25B2 / U+25BC) and on Windows they did not come from one
	// font: Segoe UI carries the up and down pair but not the left and right one, so those two fell through to MS UI
	// Gothic and drew at 5 x 12 against the others' 9 x 16 -- half the size, and visibly not a set. Painting them makes
	// "the four are the same size" a property of the geometry, and this case is what says so.
	//
	// It reads RENDERED PIXELS rather than any property of the button, because the defect was invisible to every
	// property: all four had the same widget size, the same font, and a one-character label (lessons-learned Q12).

	std::unique_ptr<TransferListEditor> editor = build ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ), QStringLiteral ( "c" ) } );

	editor->show ();

	// Every arrow enabled and none focused, so the four buttons' CHROME is identical and any difference in ink is the
	// arrow: a middle row makes Move Up and Move Down live, and an available selection makes Add live.

	chosen_list ( editor.get () )->setCurrentRow ( 1 );
	available_list ( editor.get () )->setCurrentRow ( 0 );

	QPushButton* const addButton      = button ( editor.get (), object_names::ADD_BUTTON );
	QPushButton* const removeButton   = button ( editor.get (), object_names::REMOVE_BUTTON );
	QPushButton* const moveUpButton   = button ( editor.get (), object_names::MOVE_UP_BUTTON );
	QPushButton* const moveDownButton = button ( editor.get (), object_names::MOVE_DOWN_BUTTON );

	const QList<QPushButton*> arrows = { addButton, removeButton, moveUpButton, moveDownButton };

	for ( QPushButton* const arrow : arrows )
	{
		QVERIFY ( arrow != nullptr );
		QVERIFY ( arrow->isEnabled () );

		// No text at all: the glyph is painted, and the name a screen reader announces is the accessible name. A label
		// here would be the font drawing a triangle again.

		QVERIFY ( arrow->text ().isEmpty () );
		QVERIFY ( !arrow->accessibleName ().isEmpty () );
	}

	// The chrome baseline -- an otherwise identical button that paints no arrow. Subtracting it turns "how dark is this
	// button" into "how much arrow is on it", which is what the comparison has to be about: the chrome is the larger
	// share of the ink, and a percentage taken across it would wash out even a two-fold difference in the glyph.

	QPushButton chromeReference ( editor.get () );

	chromeReference.setFixedSize ( addButton->size () );
	chromeReference.setAutoDefault ( false );

	const qreal chrome = arrow_ink ( &chromeReference );

	qreal smallest = 0.0;
	qreal largest  = 0.0;

	for ( QPushButton* const arrow : arrows )
	{
		const qreal ink = arrow_ink ( arrow ) - chrome;

		QVERIFY2 ( ink > 0.0, "an arrow button must paint something beyond its own chrome" );

		smallest = ( smallest == 0.0 ) ? ink : qMin ( smallest, ink );
		largest  = qMax ( largest, ink );
	}

	// All four are the same triangle rotated, so their areas are equal up to antialiasing of the rotated edges. The
	// tolerance is generous against that and still an order of magnitude tighter than the defect it replaces, which
	// differed by roughly a factor of three.

	QVERIFY2 ( largest <= ( smallest * 1.10 ),
	           qPrintable ( QStringLiteral ( "arrow ink varies too much across the four buttons: %1 .. %2" )
	                        .arg ( smallest ).arg ( largest ) ) );
}

QTEST_MAIN ( TestTransferListEditor )

#include "tst_transfer_list_editor.moc"

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TransferListEditor -- the editor for SettingsFieldKind::TransferList: two lists and a set of buttons producing an
//   ORDERED SUBSET of a fixed option set (SET-04, spec section 2.10, architecture.md section 8.1).
//
//   IT KNOWS NOTHING ABOUT TOOLBARS. The chosen list is on the LEFT and the available options on the right, the buttons
//   between them move an option across, and the pair beneath the left list reorders it. That the left list happens to
//   be a toolbar is entirely the field's business -- which is what lets this be a field KIND that SettingsDialog renders
//   like any other, rather than a bespoke Toolbar page. SET-02's "adding a group is one new detail page plus one
//   master-list entry" survives because of that distinction.
//
//   THE ONE ASYMMETRY IS THE REPEATABLE OPTION. Ordinary options are moved: choosing one takes it out of the available
//   list, so it can be chosen once and only once. The repeatable option (SET-04's separator) is COPIED instead -- it is
//   never consumed by being chosen and never returns by being removed, because it is a structural mark rather than a
//   thing there is one of. A field with no repeatable value simply has no such entry, and every option is unique.
//
//   ORDER IS THE RESULT, so a removal puts the option back at its OPTION-ORDER position rather than appending it: the
//   available list must not depend on the history of the session, or a user who removes and re-adds the same command
//   twice sees a list that has quietly rearranged itself.
//
//   Every mutation is funnelled through the chosen list's own model signals rather than emitted by hand at each call
//   site, so an internal drag-reorder reports itself exactly as a button press does and neither path can be forgotten.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "dialogs/settings_schema.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The parts' object names. Declared so the suite can reach them without this class exposing its widgets, and so a
	// future style sheet has something to select on.
	//-----------------------------------------------------------------------------------------------------------------

	namespace object_names
	{
		inline const QString CHOSEN_LIST      = QStringLiteral ( "transferChosenList" );
		inline const QString AVAILABLE_LIST   = QStringLiteral ( "transferAvailableList" );
		inline const QString ADD_BUTTON       = QStringLiteral ( "transferAddButton" );
		inline const QString REMOVE_BUTTON    = QStringLiteral ( "transferRemoveButton" );
		inline const QString MOVE_UP_BUTTON   = QStringLiteral ( "transferMoveUpButton" );
		inline const QString MOVE_DOWN_BUTTON = QStringLiteral ( "transferMoveDownButton" );
		inline const QString RESTORE_BUTTON   = QStringLiteral ( "transferRestoreButton" );
	}

	//*****************************************************************************************************************
	// Class: TransferListEditor
	//*****************************************************************************************************************

	class TransferListEditor : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// field supplies the options, the two captions, the repeatable value, and the default result (what Restore
		// Defaults returns to). chosen is the current value -- entries naming no option, and repeats of a non-repeatable
		// option, are skipped rather than rejected, so a settings file from a newer build still opens.

		TransferListEditor ( const SettingsField& field, const QStringList& chosen, QWidget* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QStringList chosen () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// May fire more than once for one gesture -- a reorder is a take followed by an insert, and reporting rides the
		// model signals both halves reach (see the header). The LAST emission carries the settled result, which is all
		// the dialog needs: its snapshot is written and never read back until OK.

		void chosen_changed ( const QStringList& chosen );

		//=============================================================================================================
		// Construction Helpers
		//=============================================================================================================

	private:

		void build_layout ( const SettingsField& field );

		// The four arrow buttons are built by a free function in the .cpp: their glyph is a painted triangle whose
		// direction type is local to that file (see ArrowButton), and nothing in this class's interface needs to name
		// it.

		// Fill both lists from an ordered result: chosen in the given order, available in OPTION order with whatever
		// the result did not take. Used by the constructor and by Restore Defaults alike.

		void populate ( const QStringList& chosen );

		QListWidgetItem* build_item ( const QString& value ) const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_add             ();
		void handle_remove          ();
		void handle_move_up         ();
		void handle_move_down       ();
		void handle_restore_default ();

		// The single place a change is reported and the buttons are re-evaluated, reached from the chosen list's model
		// signals so a drag-reorder and a button press arrive by the same route.

		void handle_chosen_changed ();

		void update_button_enablement ();

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	private:

		// Insert value into the available list at its OPTION-ORDER position (see the header).

		void insert_available ( const QString& value );

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		// The options in the order the available list shows them, and each value's index in that order -- which is both
		// the label / icon lookup and the position a removed option is returned to.

		QList<SettingsOption> options;
		QHash<QString, int>   optionIndexByValue;

		QString     repeatableValue;                           // Never consumed, never returned. Empty => none.
		QStringList defaultChosen;

		QListWidget* chosenList    = nullptr;
		QListWidget* availableList = nullptr;

		QPushButton* addButton      = nullptr;
		QPushButton* removeButton   = nullptr;
		QPushButton* moveUpButton   = nullptr;
		QPushButton* moveDownButton = nullptr;
		QPushButton* restoreButton  = nullptr;
	};
}

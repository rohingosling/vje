//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   edit_reporting unit tests -- what an edit command says about how it went, and through which channels (VAL-04).
//
//   The whole rule is pure, so this suite is HEADLESS and covers the phase's policy end to end. That is the point of
//   the split: the defect being fixed is that seven commands said nothing, and "said nothing" is precisely what a
//   widget test cannot assert without a message box to dismiss.
//
//   The load-bearing case is the TOTALITY one. Every other case here pins a specific message; that one pins the
//   property the family lost in the first place -- that no ( command, outcome ) pair is silent.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/edit_reporting.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QTest>

using namespace vje;

namespace
{
	// Every value of EditCommand. Listed rather than iterated, because C++ gives no way to walk an enumeration -- and
	// a list that silently falls behind the enumeration would make the totality case below weaker than it reads.
	// wording_for's switch has no default label, so ADDING a command is a compile error until it is worded; adding it
	// here is what makes it also a test failure until it is worded CORRECTLY.

	QList<EditCommand> all_edit_commands ()
	{
		return
		{
			EditCommand::AddChild,
			EditCommand::AddSibling,
			EditCommand::RenameKey,
			EditCommand::Duplicate,
			EditCommand::Delete,
			EditCommand::MoveUp,
			EditCommand::MoveDown,
			EditCommand::NormalizeArray,
			EditCommand::ArrayToObjects,
			EditCommand::ObjectsToArray,
			EditCommand::PasteNode,
			EditCommand::ChangeTypeToString,
			EditCommand::ChangeTypeToNumber,
			EditCommand::ChangeTypeToBoolean,
			EditCommand::ChangeTypeToNull,
			EditCommand::MoveNodes,
			EditCommand::SortArray,
			EditCommand::DeleteColumn,
			EditCommand::ClearColumn,
			EditCommand::ClearRow,
			EditCommand::RenameColumn
		};
	}

	QList<EditOutcome> all_edit_outcomes ()
	{
		return { EditOutcome::Applied, EditOutcome::Rejected, EditOutcome::Unchanged };
	}

	JsonPointer sample_target ()
	{
		return JsonPointer::parse ( QStringLiteral ( "/users/3" ) );
	}
}

//*********************************************************************************************************************
// Class: TestEditReporting
//*********************************************************************************************************************

class TestEditReporting : public QObject
{
	Q_OBJECT

private slots:

	//=================================================================================================================
	// Totality -- the property the family lost.
	//=================================================================================================================

	// NO ( command, outcome ) PAIR IS SILENT. This is the regression that matters: before Phase 15, Add, Duplicate,
	// Delete, both Moves, Normalize and both Converts announced nothing in any outcome, and a command that does
	// nothing and says nothing cannot be told from one that is broken.
	//
	// It covers pairs no current code path can reach -- delete_node never returns Unchanged today, for instance. That
	// is deliberate. An unreachable outcome is one refactor away from being reachable, and a table with a hole in it
	// is how the silence comes back without anyone deciding it should.

	void every_command_and_outcome_says_something ()
	{
		for ( const EditCommand command : all_edit_commands () )
		{
			for ( const EditOutcome outcome : all_edit_outcomes () )
			{
				const EditAnnouncement announcement = announce_edit ( command, outcome, sample_target () );

				QVERIFY2
				(
					!announcement.message.isEmpty (),
					qPrintable ( QStringLiteral ( "command %1 with outcome %2 announces nothing" )
					             .arg ( static_cast<int> ( command ) ).arg ( static_cast<int> ( outcome ) ) )
				);

				// A message left holding its placeholder is a wording bug that reads as a rendering fault on screen.

				QVERIFY2
				(
					!announcement.message.contains ( QStringLiteral ( "%1" ) ),
					qPrintable ( QStringLiteral ( "command %1 with outcome %2 left %%1 unsubstituted" )
					             .arg ( static_cast<int> ( command ) ).arg ( static_cast<int> ( outcome ) ) )
				);
			}
		}
	}

	//=================================================================================================================
	// Channels -- the part that must NOT vary per command.
	//=================================================================================================================

	// Phase 12.5's rule, applied to the edit family: a refusal is the one outcome a user cannot diagnose for
	// themselves, so it reaches both channels. A success and a no-op stay in the status bar -- a modal on a completed
	// command makes it read as a failure, and a modal on a no-op interrupts a user who is mid-flow.

	void only_a_refusal_raises_a_modal ()
	{
		for ( const EditCommand command : all_edit_commands () )
		{
			QVERIFY ( !announce_edit ( command, EditOutcome::Applied,   sample_target () ).is_modal () );
			QVERIFY ( !announce_edit ( command, EditOutcome::Unchanged, sample_target () ).is_modal () );

			const EditAnnouncement refusal = announce_edit ( command, EditOutcome::Rejected, sample_target () );

			QVERIFY2
			(
				refusal.is_modal (),
				qPrintable ( QStringLiteral ( "command %1 refuses without a modal" ).arg ( static_cast<int> ( command ) ) )
			);

			// The modal carries the command's own name, so the box says which command refused rather than "Error".

			QVERIFY ( !refusal.modalTitle.isEmpty () );
		}
	}

	//=================================================================================================================
	// Naming the node.
	//=================================================================================================================

	void an_applied_edit_names_its_target ()
	{
		const EditAnnouncement announcement =
			announce_edit ( EditCommand::Delete, EditOutcome::Applied, sample_target () );

		QCOMPARE ( announcement.message, QStringLiteral ( "Deleted /users/3" ) );
	}

	// The root's RFC 6901 pointer is the empty string, so a message built from it alone would trail off after the
	// verb -- "Deleted " with nothing after it. FindController's Go To hit the same problem first and now shares this
	// spelling, so the two cannot describe one node two ways.

	void the_root_is_named_in_words_rather_than_as_an_empty_pointer ()
	{
		QCOMPARE ( node_display_text ( JsonPointer () ), QStringLiteral ( "the document root" ) );

		const EditAnnouncement announcement =
			announce_edit ( EditCommand::NormalizeArray, EditOutcome::Applied, JsonPointer () );

		QCOMPARE ( announcement.message, QStringLiteral ( "Normalized the document root" ) );
	}

	void a_non_root_pointer_is_named_by_its_rfc_6901_text ()
	{
		QCOMPARE ( node_display_text ( sample_target () ), QStringLiteral ( "/users/3" ) );
	}

	//=================================================================================================================
	// Wording that has to be right rather than merely present.
	//=================================================================================================================

	// THE MOTIVATING CASE. Normalizing an array whose elements already carry the same members changes nothing --
	// normalize_array_elements is idempotent -- and until Phase 15 that outcome dirtied the document, added an undo
	// step, and said nothing at all. The message has to state the reason, because "nothing happened" is exactly what
	// the user is trying to tell apart from a broken command.

	void an_unchanged_normalize_explains_itself ()
	{
		const EditAnnouncement announcement =
			announce_edit ( EditCommand::NormalizeArray, EditOutcome::Unchanged, sample_target () );

		QCOMPARE ( announcement.message, QStringLiteral ( "Every element already carries the same members." ) );
		QVERIFY  ( !announcement.is_modal () );
	}

	// EDIT-08's two directions are separate commands precisely so these two differ. A user who is told only "nothing
	// moved" cannot tell a boundary from a dead shortcut, and cannot tell which boundary.

	void the_two_move_directions_report_different_boundaries ()
	{
		const QString up   = announce_edit ( EditCommand::MoveUp,   EditOutcome::Unchanged, sample_target () ).message;
		const QString down = announce_edit ( EditCommand::MoveDown, EditOutcome::Unchanged, sample_target () ).message;

		QCOMPARE ( up,   QStringLiteral ( "Already the first item." ) );
		QCOMPARE ( down, QStringLiteral ( "Already the last item." ) );
		QVERIFY  ( up != down );
	}

	// The two refusals that existed before this phase, preserved verbatim -- they were the only correct reporting in
	// the family, and the rule absorbed them rather than replacing them with something more generic.

	void the_pre_existing_refusals_keep_their_wording ()
	{
		QCOMPARE
		(
			announce_edit ( EditCommand::RenameKey, EditOutcome::Rejected, sample_target () ).message,
			QStringLiteral ( "That key already exists in this object." )
		);

		QCOMPARE
		(
			announce_edit ( EditCommand::PasteNode, EditOutcome::Rejected, sample_target () ).message,
			QStringLiteral ( "The clipboard content cannot be pasted here." )
		);
	}

	// Each refusal must state the precondition UndoController actually applies. These two are read straight off its
	// implementation: normalize_array refuses anything that is not an array of objects, and delete_node refuses the
	// root because it has no parent to be removed from.

	void a_refusal_states_the_precondition_that_failed ()
	{
		QCOMPARE
		(
			announce_edit ( EditCommand::NormalizeArray, EditOutcome::Rejected, sample_target () ).message,
			QStringLiteral ( "Only an array whose elements are all objects can be normalized." )
		);

		QCOMPARE
		(
			announce_edit ( EditCommand::Delete, EditOutcome::Rejected, JsonPointer () ).message,
			QStringLiteral ( "The document root cannot be deleted." )
		);
	}

	// Distinct commands must not collapse onto one another's wording, which is the failure a shared "Edit refused"
	// string would have. Checked over the whole set rather than by spot comparison.

	void no_two_commands_share_a_refusal_message ()
	{
		QSet<QString> seen;

		for ( const EditCommand command : all_edit_commands () )
		{
			const QString title = announce_edit ( command, EditOutcome::Rejected, sample_target () ).modalTitle;

			QVERIFY2
			(
				!seen.contains ( title ),
				qPrintable ( QStringLiteral ( "two commands share the modal title \"%1\"" ).arg ( title ) )
			);

			seen.insert ( title );
		}
	}

	//=================================================================================================================
	// Convert To (EDIT-09)
	//=================================================================================================================

	// The kind -> command mapping, because it is the join between three surfaces -- the menu item that is clicked, the
	// JsonKind handed to UndoController::change_type, and the wording reported afterwards. A mapping that slipped by
	// one would convert correctly and then say it had done something else, which no other case here would catch.

	void each_target_kind_maps_to_its_own_command ()
	{
		QCOMPARE ( change_type_command ( JsonKind::String  ), EditCommand::ChangeTypeToString  );
		QCOMPARE ( change_type_command ( JsonKind::Number  ), EditCommand::ChangeTypeToNumber  );
		QCOMPARE ( change_type_command ( JsonKind::Boolean ), EditCommand::ChangeTypeToBoolean );
		QCOMPARE ( change_type_command ( JsonKind::Null    ), EditCommand::ChangeTypeToNull    );
	}

	// EDIT-09's reason for offering a conversion to the selection's OWN current type: masking it would make the
	// submenu's contents change with the selection. Offering it is only defensible if the no-op then explains itself,
	// and it has to name the TYPE -- "Nothing to do" over four commands would leave the user guessing which one they
	// pressed.

	void an_unchanged_conversion_names_the_type_it_is_already ()
	{
		const QList<QPair<EditCommand, QString>> expected =
		{
			{ EditCommand::ChangeTypeToString,  QStringLiteral ( "string"  ) },
			{ EditCommand::ChangeTypeToNumber,  QStringLiteral ( "number"  ) },
			{ EditCommand::ChangeTypeToBoolean, QStringLiteral ( "boolean" ) },
			{ EditCommand::ChangeTypeToNull,    QStringLiteral ( "null"    ) }
		};

		for ( const auto& [ command, typeName ] : expected )
		{
			const EditAnnouncement announcement = announce_edit ( command, EditOutcome::Unchanged, sample_target () );

			QVERIFY2
			(
				announcement.message.contains ( typeName ),
				qPrintable ( QStringLiteral ( "\"%1\" does not name %2" ).arg ( announcement.message, typeName ) )
			);

			// A no-op is not an error, and the user is mid-flow -- so the status bar and nothing else (VAL-05).

			QVERIFY ( !announcement.is_modal () );
		}
	}

	// The four applied messages differ, for the same reason: with the submenu closed, the status bar is the only thing
	// that says which of them ran.

	void the_four_conversions_report_four_different_successes ()
	{
		QSet<QString> seen;

		const QList<EditCommand> conversions =
		{
			EditCommand::ChangeTypeToString,
			EditCommand::ChangeTypeToNumber,
			EditCommand::ChangeTypeToBoolean,
			EditCommand::ChangeTypeToNull
		};

		for ( const EditCommand command : conversions )
		{
			const QString message = announce_edit ( command, EditOutcome::Applied, sample_target () ).message;

			QVERIFY2
			(
				!seen.contains ( message ),
				qPrintable ( QStringLiteral ( "two conversions report \"%1\"" ).arg ( message ) )
			);

			seen.insert ( message );
		}
	}

	//=================================================================================================================
	// The subject count (Phase 15f -- EDIT-14 / EDIT-10)
	//=================================================================================================================

	// One node is NAMED and several are COUNTED. The singular is node_display_text's, unchanged, which is what makes
	// EDIT-14 invisible until it is used: a one-node delete reports exactly what it always did.

	void one_node_is_named_and_several_are_counted ()
	{
		QCOMPARE ( subject_display_text ( sample_target (), 1 ), node_display_text ( sample_target () ) );
		QCOMPARE ( subject_display_text ( sample_target (), 4 ), QStringLiteral ( "4 nodes" ) );

		// The literal "(s)" is what an untranslated tr ( "%n node(s)" ) would leave on screen (lesson Q42), and every
		// build this project ships is untranslated.

		QVERIFY ( !subject_display_text ( sample_target (), 4 ).contains ( QStringLiteral ( "(s)" ) ) );
	}

	void a_multi_node_command_reports_the_count_rather_than_a_pointer ()
	{
		const QString single = announce_edit ( EditCommand::Delete, EditOutcome::Applied, sample_target () ).message;
		const QString many   = announce_edit ( EditCommand::Delete, EditOutcome::Applied, sample_target (), 4 ).message;

		QVERIFY ( single.contains ( QStringLiteral ( "/users/3" ) ) );
		QVERIFY ( many.contains   ( QStringLiteral ( "4 nodes" ) ) );
		QVERIFY ( !many.contains  ( QStringLiteral ( "/users/3" ) ) );
	}

	//=================================================================================================================
	// EDIT-10's reorder
	//=================================================================================================================

	// The reason MoveNodes is its own command rather than MoveUp / MoveDown with a longer reach. A drag's no-op is
	// that the run landed where it already was -- which happens ANYWHERE in a container and says nothing about edges,
	// so it must not borrow the edge wording that Move Up and Move Down carry.

	void the_reorder_no_op_does_not_borrow_the_edge_wording ()
	{
		const QString reorder  = announce_edit ( EditCommand::MoveNodes, EditOutcome::Unchanged, sample_target () ).message;
		const QString moveUp   = announce_edit ( EditCommand::MoveUp,    EditOutcome::Unchanged, sample_target () ).message;
		const QString moveDown = announce_edit ( EditCommand::MoveDown,  EditOutcome::Unchanged, sample_target () ).message;

		QVERIFY ( reorder != moveUp );
		QVERIFY ( reorder != moveDown );

		QVERIFY2 ( !reorder.contains ( QStringLiteral ( "first" ) ), qPrintable ( reorder ) );
		QVERIFY2 ( !reorder.contains ( QStringLiteral ( "last" ) ),  qPrintable ( reorder ) );

		// And it is a status-bar message, not a modal: a drag that went nowhere is not an error (VAL-05).

		QVERIFY ( !announce_edit ( EditCommand::MoveNodes, EditOutcome::Unchanged, sample_target () ).is_modal () );
	}

	void a_refused_reorder_states_the_same_parent_rule ()
	{
		const EditAnnouncement announcement = announce_edit ( EditCommand::MoveNodes, EditOutcome::Rejected, sample_target () );

		QVERIFY  ( announcement.is_modal () );
		QVERIFY2 ( announcement.message.contains ( QStringLiteral ( "parent" ) ), qPrintable ( announcement.message ) );
	}
};

QTEST_APPLESS_MAIN ( TestEditReporting )

#include "tst_edit_reporting.moc"

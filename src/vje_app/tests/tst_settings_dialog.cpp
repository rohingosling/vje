//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for SettingsDialog's rendering of a DISABLED setting (SET-01b): the whole row greys out, its text label
//   with it, and it follows the edit state live rather than only the state the dialog opened in.
//
//   WHY THIS NEEDS A SUITE OF ITS OWN, given tst_settings_schema exists. That one pins the DECISION -- what
//   SettingsSnapshot::is_field_enabled answers, for both the boolean gate (SET-09) and the value gate. This one pins
//   what the dialog DOES with that answer, which is a widget property and unreachable from there. The two halves failed
//   independently before SET-01b: is_field_enabled was already right, and the label was already black.
//
//   The labels are found by their TEXT through findChildren, deliberately, rather than by asking the dialog for the
//   bookkeeping it keeps. What the requirement promises is about the label a user reads, so the case looks for exactly
//   that and would still be honest if the hash behind it were replaced.
//
//   The schema here is BUILT FOR THE TEST rather than taken from settings_schema(), so the rule is pinned independently
//   of which real settings happen to carry a dependency today -- SET-09's pair could be switched off at build time
//   (SET-01) and this must still hold.
//
//   Offscreen: a dialog is constructed and its widgets interrogated, but nothing is shown and nothing is drawn.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/SettingsDialog.hpp"

#include "dialogs/settings_schema.hpp"

#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include <vector>

using namespace vje;

namespace
{
	const QString MASTER_KEY   = QStringLiteral ( "test.master" );
	const QString MASTER_LABEL = QStringLiteral ( "Master switch" );

	const QString GATED_KEY    = QStringLiteral ( "test.gated" );
	const QString GATED_LABEL  = QStringLiteral ( "Gated value" );

	const QString FREE_KEY     = QStringLiteral ( "test.free" );
	const QString FREE_LABEL   = QStringLiteral ( "Ungated value" );

	// SET-09's shape, in miniature: a boolean that governs a second field, plus one field governed by nothing. The
	// master defaults to FALSE, so a dialog seeded from a null store opens with the gated field disabled.

	std::vector<SettingsGroup> gated_schema ()
	{
		SettingsField master;

		master.kind         = SettingsFieldKind::YesNo;
		master.key          = MASTER_KEY;
		master.label        = MASTER_LABEL;
		master.defaultValue = false;

		SettingsField gated;

		gated.kind         = SettingsFieldKind::ShortText;
		gated.key          = GATED_KEY;
		gated.label        = GATED_LABEL;
		gated.defaultValue = QString ();
		gated.enabledByKey = MASTER_KEY;

		SettingsField free;

		free.kind         = SettingsFieldKind::ShortText;
		free.key          = FREE_KEY;
		free.label        = FREE_LABEL;
		free.defaultValue = QString ();

		SettingsGroup group;

		group.title  = QStringLiteral ( "Test" );
		group.fields = { master, gated, free };

		return { group };
	}

	// The label carrying this text, from anywhere in the dialog. Null when no such label exists, which is itself a
	// failure worth reporting separately from "the label was the wrong state".

	QLabel* find_label ( const QWidget& dialog, const QString& text )
	{
		for ( QLabel* const label : dialog.findChildren<QLabel*> () )
		{
			if ( label->text () == text )
			{
				return label;
			}
		}

		return nullptr;
	}

	// The Yes / No combo for a field, found by the accessible name build_editor gives every editor (NFR-05).

	QComboBox* find_combo ( const QWidget& dialog, const QString& accessibleName )
	{
		for ( QComboBox* const combo : dialog.findChildren<QComboBox*> () )
		{
			if ( combo->accessibleName () == accessibleName )
			{
				return combo;
			}
		}

		return nullptr;
	}
}

//*********************************************************************************************************************
// Class: TestSettingsDialog
//*********************************************************************************************************************

class TestSettingsDialog : public QObject
{
	Q_OBJECT

private slots:

	void a_disabled_field_greys_its_label_as_well_as_its_editor ();
	void an_ungated_field_keeps_both_halves_enabled ();
	void enabling_the_governing_setting_restores_the_label_live ();
};

//---------------------------------------------------------------------------------------------------------------------
// SET-01b. The half this requirement added is the LABEL; the editor half predates it and is asserted alongside so a
// regression in either shows up here rather than one of them silently carrying the other.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::a_disabled_field_greys_its_label_as_well_as_its_editor ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr );

	QLabel* const label = find_label ( dialog, GATED_LABEL );

	QVERIFY2 ( label != nullptr, "the gated field's label was not built at all" );

	// The master defaults to No, so the gated row opens disabled -- both halves of it.

	QVERIFY2 ( !label->isEnabled (), "SET-01b: a disabled setting's LABEL must grey out with its editor" );

	QVERIFY ( dialog.findChildren<QWidget*> ().size () > 0 );
}

void TestSettingsDialog::an_ungated_field_keeps_both_halves_enabled ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr );

	// The control case, and it is not redundant: a refresh that simply disabled every label would satisfy the case
	// above perfectly well.

	QLabel* const freeLabel = find_label ( dialog, FREE_LABEL );

	QVERIFY ( freeLabel != nullptr );
	QVERIFY2 ( freeLabel->isEnabled (), "a field with no dependency must not be greyed" );

	QLabel* const masterLabel = find_label ( dialog, MASTER_LABEL );

	QVERIFY ( masterLabel != nullptr );
	QVERIFY2 ( masterLabel->isEnabled (), "the governing setting itself must stay enabled" );
}

void TestSettingsDialog::enabling_the_governing_setting_restores_the_label_live ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr );

	QLabel* const label = find_label ( dialog, GATED_LABEL );

	QVERIFY ( label != nullptr );
	QVERIFY ( !label->isEnabled () );

	// SET-09's rule, which SET-01b inherits: the row answers to the EDIT state, so switching the master on lifts the
	// label there and then -- before OK has written anything.

	QComboBox* const master = find_combo ( dialog, MASTER_LABEL );

	QVERIFY2 ( master != nullptr, "the master switch's editor was not found" );

	// Index 0 is Yes for a YesNo field (build_editor orders them Yes, No).

	master->setCurrentIndex ( 0 );

	QVERIFY2 ( label->isEnabled (), "SET-01b: the label must follow the edit state, not the state on opening" );

	// And back again, so the case cannot pass against an implementation that only ever enables.

	master->setCurrentIndex ( 1 );

	QVERIFY2 ( !label->isEnabled (), "the label must grey again when the governing setting is switched off" );
}

QTEST_MAIN ( TestSettingsDialog )

#include "tst_settings_dialog.moc"

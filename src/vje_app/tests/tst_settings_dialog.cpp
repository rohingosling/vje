//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for what SettingsDialog DOES with its schema -- the widget half, which tst_settings_schema cannot reach:
//
//     - SET-01b: a disabled setting's whole row greys out, its text label with it, following the edit state live.
//     - SET-01c: each titled section is a group box holding exactly its rows; every editor on every page shares one
//       left edge and one width, filling its box to the padding; Tab runs down the boxes in reading order; and the
//       tallest page fits the dialog's default size.
//
//   WHY THIS NEEDS A SUITE OF ITS OWN, given tst_settings_schema exists. That one pins the DECISIONS -- what
//   is_field_enabled answers, which box a setting is in. This one pins what the dialog does with them, which is widget
//   state and geometry and unreachable from there. The two halves failed independently before SET-01b: is_field_enabled
//   was already right, and the label was already black.
//
//   The labels are found by their TEXT through findChildren, deliberately, rather than by asking the dialog for the
//   bookkeeping it keeps. What the requirement promises is about the label a user reads, so the case looks for exactly
//   that and would still be honest if the hash behind it were replaced. A row's editor is its label's buddy.
//
//   Two schemas. The SET-01b cases and the box-membership case use one BUILT FOR THE TEST, so the rule is pinned
//   independently of which real settings carry a dependency today. The geometry cases use the REAL schema, because the
//   claim is about the real dialog: labels of different lengths on different pages is what makes the shared column
//   worth asserting, and the real pages are what have to fit.
//
//   Offscreen: the geometry cases show the dialog, since only a shown layout has geometry worth reading.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/SettingsDialog.hpp"

#include "AppConfig.hpp"
#include "dialogs/GroupBox.hpp"
#include "dialogs/settings_schema.hpp"
#include "services/IDialogService.hpp"
#include "services/ThemeService.hpp"
#include "style/tooltip_text.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QValidator>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QSet>
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

	const QString COUNT_KEY    = QStringLiteral ( "test.count" );
	const QString COUNT_LABEL  = QStringLiteral ( "A count" );

	const QString FIRST_BOX    = QStringLiteral ( "First Box" );
	const QString SECOND_BOX   = QStringLiteral ( "Second Box" );

	// SET-09's shape, in miniature: a boolean that governs a second field, plus one field governed by nothing. The
	// master defaults to FALSE, so a dialog seeded from a null store opens with the gated field disabled.
	//
	// In two BOXES (SET-01c) -- the governing row and the one it governs share a box, as SET-09's do, and the free row
	// and a count sit in a second -- so the SET-01b cases hold inside a box, where every real row now is.

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

		SettingsField count;

		count.kind           = SettingsFieldKind::Integer;
		count.key            = COUNT_KEY;
		count.label          = COUNT_LABEL;
		count.defaultValue   = 1;
		count.minimumInteger = 0;
		count.maximumInteger = 9;

		SettingsSection first;

		first.title  = FIRST_BOX;
		first.fields = { master, gated };

		SettingsSection second;

		second.title  = SECOND_BOX;
		second.fields = { free, count };

		SettingsGroup group;

		group.title    = QStringLiteral ( "Test" );
		group.sections = { first, second };

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

	// The group box with this title. Null when there is none.

	GroupBox* find_box ( const QWidget& dialog, const QString& title )
	{
		for ( GroupBox* const box : dialog.findChildren<GroupBox*> () )
		{
			if ( box->plain_title () == title )
			{
				return box;
			}
		}

		return nullptr;
	}

	// Every row label in a widget, in the order the rows were built: a row's label is the one with a buddy, and the
	// buddy is its editor.

	QList<QLabel*> row_labels ( const QWidget& root )
	{
		QList<QLabel*> labels;

		for ( QLabel* const label : root.findChildren<QLabel*> () )
		{
			if ( label->buddy () != nullptr )
			{
				labels.append ( label );
			}
		}

		return labels;
	}

	// SET-14a's colour picker, answered by the case: it records what it was opened on and returns what it is told to --
	// an invalid colour being a cancel. Every other modal is unreachable from the Settings dialog's colour row.

	class ColourPickerFake : public IDialogService
	{
	public:

		QColor  answer;
		QColor  initialSeen;
		QString titleSeen;
		int     calls = 0;

		QColor choose_colour ( const QString& title, const QColor& initial ) override
		{
			++calls;

			titleSeen   = title;
			initialSeen = initial;

			return answer;
		}

		QString choose_file_to_open ( const QString&, const QString&, const QString& ) override { return QString (); }
		QString choose_file_to_save ( const QString&, const QString&, const QString& ) override { return QString (); }
		QString choose_folder       ( const QString&, const QString& ) override                 { return QString (); }

		SaveChangesAnswer ask_save_changes ( const QString& ) override { return SaveChangesAnswer::Cancel; }

		void show_error       ( const QString&, const QString& ) override {}
		void show_warning     ( const QString&, const QString& ) override {}
		void show_information ( const QString&, const QString& ) override {}

		bool confirm ( const QString&, const QString& ) override { return false; }

		std::optional<QString> ask_text ( const QString&, const QString&, const QString& ) override { return std::nullopt; }

		bool run_xml_import_dialog ( XmlImportController&, const QString& ) override { return false; }
		bool run_page_setup_dialog ( QPrinter& ) override                            { return false; }
		bool run_print_dialog      ( QPrinter& ) override                            { return false; }
	};

	// The services a SET-14a case needs: a store to apply to, and a theme in the given state.

	struct ColourFixture
	{
		QTemporaryDir directory;
		SettingsStore store { directory.filePath ( QStringLiteral ( "settings.json" ) ) };
		ThemeService  theme { &store };

		explicit ColourFixture ( bool dark )
		{
			theme.apply ();
			theme.set_theme ( dark ? Theme::Dark : Theme::Light );
		}
	};

	QLineEdit* colour_box ( const QWidget& dialog )
	{
		for ( QLineEdit* const lineEdit : dialog.findChildren<QLineEdit*> () )
		{
			if ( lineEdit->accessibleName () == QStringLiteral ( "Unsaved change color" ) )
			{
				return lineEdit;
			}
		}

		return nullptr;
	}

	QPushButton* choose_button ( const QLineEdit& box )
	{
		for ( QPushButton* const button : box.parentWidget ()->findChildren<QPushButton*> () )
		{
			if ( button->text () == QStringLiteral ( "Choose..." ) )
			{
				return button;
			}
		}

		return nullptr;
	}

	void press_ok ( const QWidget& dialog )
	{
		dialog.findChild<QDialogButtonBox*> ()->button ( QDialogButtonBox::Ok )->click ();
	}

	// Show the page at this master-list row and let its layout run -- a QStackedWidget lays out only the page it is
	// showing, so a page's geometry means nothing until it has been shown.

	void show_page ( QListWidget& groupList, int row )
	{
		groupList.setCurrentRow ( row );

		QApplication::processEvents ();
	}
}

//*********************************************************************************************************************
// Class: TestSettingsDialog
//*********************************************************************************************************************

class TestSettingsDialog : public QObject
{
	Q_OBJECT

private slots:

	// SET-01b.

	void a_disabled_field_greys_its_label_as_well_as_its_editor ();
	void an_ungated_field_keeps_both_halves_enabled ();
	void enabling_the_governing_setting_restores_the_label_live ();

	// SET-01c.

	void each_titled_section_is_a_box_holding_exactly_its_rows ();
	void every_editor_in_the_dialog_shares_one_left_edge_and_one_width ();
	void every_editor_fills_its_box_to_the_padding ();
	void tab_runs_down_the_boxes_in_reading_order ();
	void the_tallest_page_fits_the_default_size ();

	// STYLE-17.

	void every_row_says_what_its_setting_does ();

	// SET-14a.

	void the_colour_row_speaks_the_theme_showing_data ();
	void the_colour_row_speaks_the_theme_showing ();
	void a_colour_typed_is_stored_as_the_dark_theme_s ();
	void an_incomplete_colour_is_never_stored_and_reverts ();
	void choose_opens_the_picker_on_the_colour_shown ();
};

//---------------------------------------------------------------------------------------------------------------------
// SET-01b. The half this requirement added is the LABEL; the editor half predates it and is asserted alongside so a
// regression in either shows up here rather than one of them silently carrying the other.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::a_disabled_field_greys_its_label_as_well_as_its_editor ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr, QIcon () );

	QLabel* const label = find_label ( dialog, GATED_LABEL );

	QVERIFY2 ( label != nullptr, "the gated field's label was not built at all" );

	// The master defaults to No, so the gated row opens disabled -- both halves of it.

	QVERIFY2 ( !label->isEnabled (), "SET-01b: a disabled setting's LABEL must grey out with its editor" );
	QVERIFY2 ( !label->buddy ()->isEnabled (), "and its editor with it" );
}

void TestSettingsDialog::an_ungated_field_keeps_both_halves_enabled ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr, QIcon () );

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
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr, QIcon () );

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

//---------------------------------------------------------------------------------------------------------------------
// SET-01c. A titled section is a group box of that title, and the box holds its own rows -- labels AND editors -- and
// no others. Asserted by ancestry rather than by position, since "in the box" is a parent relation before it is a
// geometric one.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::each_titled_section_is_a_box_holding_exactly_its_rows ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr, QIcon () );

	QCOMPARE ( dialog.findChildren<GroupBox*> ().size (), 2 );

	GroupBox* const first  = find_box ( dialog, FIRST_BOX );
	GroupBox* const second = find_box ( dialog, SECOND_BOX );

	QVERIFY2 ( first  != nullptr, "the first section was not drawn as a box" );
	QVERIFY2 ( second != nullptr, "the second section was not drawn as a box" );

	const QMap<QString, GroupBox*> expectedBox =
	{
		{ MASTER_LABEL, first },
		{ GATED_LABEL,  first },
		{ FREE_LABEL,   second },
		{ COUNT_LABEL,  second }
	};

	for ( auto entry = expectedBox.constBegin (); entry != expectedBox.constEnd (); ++entry )
	{
		QLabel* const label = find_label ( dialog, entry.key () );

		QVERIFY ( label != nullptr );

		GroupBox* const box   = entry.value ();
		GroupBox* const other = ( box == first ) ? second : first;

		QVERIFY2 ( box->isAncestorOf ( label ),           qPrintable ( entry.key () + QStringLiteral ( ": label not in its box" ) ) );
		QVERIFY2 ( box->isAncestorOf ( label->buddy () ), qPrintable ( entry.key () + QStringLiteral ( ": editor not in its box" ) ) );
		QVERIFY2 ( !other->isAncestorOf ( label ),        qPrintable ( entry.key () + QStringLiteral ( ": label in the other box" ) ) );
	}

	// Exactly its rows: each box holds two row labels, no more.

	QCOMPARE ( row_labels ( *first ).size (),  2 );
	QCOMPARE ( row_labels ( *second ).size (), 2 );

	// Rounded under both interface styles (SET-01c, revised 2026-09-27) -- the null store here answers the default, so
	// the dialog must not be asking the style at all.

	QVERIFY ( first->corners_rounded () );
	QVERIFY ( second->corners_rounded () );
}

//---------------------------------------------------------------------------------------------------------------------
// SET-01c's one label column, on the real dialog: every editor on EVERY page starts at the same x and has the same
// width. The real schema is what makes this worth asserting -- "Check for updates automatically" on one page and
// "Theme" on another -- since a column sized per box, or per page, would put those two pages' editors in different
// places. A row's editor is its label's buddy, mapped to the dialog so pages and boxes are compared in one frame.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::every_editor_in_the_dialog_shares_one_left_edge_and_one_width ()
{
	SettingsDialog dialog ( settings_schema (), nullptr, nullptr, nullptr, QIcon () );

	dialog.show ();

	QListWidget* const groupList = dialog.findChild<QListWidget*> ();

	QVERIFY ( groupList != nullptr );

	QSet<int>   lefts;
	QSet<int>   widths;
	QStringList measured;

	int pagesWithRows = 0;

	for ( int row = 0; row < groupList->count (); ++row )
	{
		show_page ( *groupList, row );

		bool pageHasRows = false;

		for ( QLabel* const label : row_labels ( dialog ) )
		{
			QWidget* const editor = label->buddy ();

			if ( !editor->isVisible () )
			{
				continue;                                          // On a page not currently shown.
			}

			pageHasRows = true;

			const int left = editor->mapTo ( &dialog, QPoint ( 0, 0 ) ).x ();

			lefts.insert  ( left );
			widths.insert ( editor->width () );

			measured.append ( QStringLiteral ( "%1: x %2, width %3" ).arg ( label->text () ).arg ( left ).arg ( editor->width () ) );
		}

		pagesWithRows += pageHasRows ? 1 : 0;
	}

	// Several pages, or the case measured one page against itself.

	QVERIFY2 ( pagesWithRows >= 5, qPrintable ( QString::number ( pagesWithRows ) ) );

	QVERIFY2 ( lefts.size ()  == 1, qPrintable ( measured.join ( QStringLiteral ( "\n" ) ) ) );
	QVERIFY2 ( widths.size () == 1, qPrintable ( measured.join ( QStringLiteral ( "\n" ) ) ) );

	// And the column is the widest label's: every row label is exactly that wide. Each label's natural width is measured
	// on a fresh label carrying the same text and font, so nothing the dialog did to its own labels can leak into the
	// width they are compared against.

	int widest = 0;

	for ( QLabel* const label : row_labels ( dialog ) )
	{
		QLabel probe ( label->text () );

		probe.setFont ( label->font () );

		widest = qMax ( widest, probe.sizeHint ().width () );
	}

	QVERIFY ( widest > 0 );

	for ( QLabel* const label : row_labels ( dialog ) )
	{
		QVERIFY2 ( label->width () == widest, qPrintable ( QStringLiteral ( "%1 is %2 wide, the column %3" ).arg ( label->text () ).arg ( label->width () ).arg ( widest ) ) );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// SET-01c's fill: every editor runs to its box's right padding, and the rows sit inside the padding on all four sides.
// A Yes / No dropdown is asserted WIDER than its own size hint, so "fills" cannot pass by the editors merely happening
// to be one width -- which is what they were at their size hints on a page of Yes / No rows alone.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::every_editor_fills_its_box_to_the_padding ()
{
	SettingsDialog dialog ( settings_schema (), nullptr, nullptr, nullptr, QIcon () );

	dialog.show ();

	QListWidget* const groupList = dialog.findChild<QListWidget*> ();

	QVERIFY ( groupList != nullptr );

	int boxesMeasured      = 0;
	int yesNoWiderThanHint = 0;

	for ( int row = 0; row < groupList->count (); ++row )
	{
		show_page ( *groupList, row );

		for ( GroupBox* const box : dialog.findChildren<GroupBox*> () )
		{
			if ( !box->isVisible () )
			{
				continue;
			}

			++boxesMeasured;

			const QRect inside = box->contentsRect ();

			QRect rows;

			for ( QLabel* const label : row_labels ( *box ) )
			{
				QWidget* const editor = label->buddy ();

				// To the right padding, exactly.

				QVERIFY2
				(
					editor->geometry ().right () == inside.right (),
					qPrintable ( QStringLiteral ( "%1 ends at %2, the padding at %3" ).arg ( label->text () ).arg ( editor->geometry ().right () ).arg ( inside.right () ) )
				);

				rows = rows.united ( label->geometry () ).united ( editor->geometry () );

				QComboBox* const combo = qobject_cast<QComboBox*> ( editor );

				if ( ( combo != nullptr ) && ( combo->count () == 2 ) && ( combo->itemText ( 0 ) == QStringLiteral ( "Yes" ) ) )
				{
					yesNoWiderThanHint += ( combo->width () > combo->sizeHint ().width () ) ? 1 : 0;
				}
			}

			// The rows fill the box's inside exactly: the padding is the box's, on all four sides, and nothing inside
			// adds to it.

			QVERIFY2
			(
				rows == inside,
				qPrintable ( QStringLiteral ( "\"%1\": rows (%2, %3) to (%4, %5), inside (%6, %7) to (%8, %9)" )
				             .arg ( box->plain_title () )
				             .arg ( rows.left () ).arg ( rows.top () ).arg ( rows.right () ).arg ( rows.bottom () )
				             .arg ( inside.left () ).arg ( inside.top () ).arg ( inside.right () ).arg ( inside.bottom () ) )
			);
		}
	}

	QVERIFY ( boxesMeasured >= 10 );
	QVERIFY ( yesNoWiderThanHint > 0 );
}

//---------------------------------------------------------------------------------------------------------------------
// NFR-05 under SET-01c: Tab runs down the boxes in the order they are read -- every row of the first box, then the
// next box. The editors are created on the dialog and reparented into their boxes by the layouts, so the focus chain's
// order after that is a toolkit behaviour and is measured rather than assumed. A box itself is never a Tab stop.
//
// The walk starts at the MASTER LIST, which is where a user tabbing into a page starts, and not at the first editor.
// The chain is a cycle, so a walk from an editor reads every rotation of the right order as the right order: it could
// not tell the second box's rows coming before the first's (found by the neuter sweep, lesson D10).
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::tab_runs_down_the_boxes_in_reading_order ()
{
	SettingsDialog dialog ( gated_schema (), nullptr, nullptr, nullptr, QIcon () );

	dialog.show ();

	QList<QWidget*> readingOrder;

	for ( const QString& text : { MASTER_LABEL, GATED_LABEL, FREE_LABEL, COUNT_LABEL } )
	{
		QLabel* const label = find_label ( dialog, text );

		QVERIFY ( label != nullptr );

		readingOrder.append ( label->buddy () );
	}

	QListWidget* const groupList = dialog.findChild<QListWidget*> ();

	QVERIFY ( groupList != nullptr );

	// Walk the focus chain once round from the master list, keeping the editors in the order the chain reaches them.

	QList<QWidget*> chainOrder;

	for ( QWidget* next = groupList->nextInFocusChain (); next != groupList; next = next->nextInFocusChain () )
	{
		if ( readingOrder.contains ( next ) && !chainOrder.contains ( next ) )
		{
			chainOrder.append ( next );
		}
	}

	QCOMPARE ( chainOrder, readingOrder );

	for ( GroupBox* const box : dialog.findChildren<GroupBox*> () )
	{
		QCOMPARE ( box->focusPolicy (), Qt::NoFocus );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// The boxes add height -- a title line and padding per box -- so the tallest page must still fit the size the dialog
// opens at. A guard for the next setting too: the page that outgrows the default fails here rather than opening a
// dialog taller than config::settings_dialog says.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::the_tallest_page_fits_the_default_size ()
{
	// The dialog as the window builds it, the Toolbar group included -- its transfer list sized the default.

	QAction undo ( QStringLiteral ( "&Undo" ) );
	QAction redo ( QStringLiteral ( "&Redo" ) );

	const std::vector<ToolbarCommand> catalogue =
	{
		{ &undo, toolbar_names::UNDO },
		{ &redo, toolbar_names::REDO }
	};

	SettingsDialog dialog ( settings_schema_with_toolbar ( catalogue ), nullptr, nullptr, nullptr, QIcon () );

	const QSize minimum = dialog.minimumSizeHint ();

	QVERIFY2
	(
		minimum.height () <= config::settings_dialog::DEFAULT_HEIGHT,
		qPrintable ( QStringLiteral ( "the dialog needs %1 px, and opens at %2" ).arg ( minimum.height () ).arg ( config::settings_dialog::DEFAULT_HEIGHT ) )
	);

	QVERIFY2
	(
		minimum.width () <= config::settings_dialog::DEFAULT_WIDTH,
		qPrintable ( QStringLiteral ( "the dialog needs %1 px, and opens at %2" ).arg ( minimum.width () ).arg ( config::settings_dialog::DEFAULT_WIDTH ) )
	);

	dialog.show ();

	QCOMPARE ( dialog.size (), QSize ( config::settings_dialog::DEFAULT_WIDTH, config::settings_dialog::DEFAULT_HEIGHT ) );
}

QTEST_MAIN ( TestSettingsDialog )

//---------------------------------------------------------------------------------------------------------------------
// STYLE-17. Every row of the real dialog carries its setting's description as a tooltip, on the label and the editor
// alike, through the one formatter -- and never the label's own text. Walked over every row the dialog built, so a row
// added later is covered without naming it.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::every_row_says_what_its_setting_does ()
{
	const std::vector<SettingsGroup>& groups = settings_schema ();

	SettingsDialog dialog ( groups, nullptr, nullptr, nullptr, QIcon () );

	// What each label may carry. Several rows share a label ("Edit on", "Wrap strings"), so a label maps to every
	// description a row of that name has.

	QMultiHash<QString, QString> tooltipsByLabel;
	int                          rows = 0;

	for ( const SettingsField* const field : settings_fields ( groups ) )
	{
		if ( !settings_field_spans_page ( field->kind ) )
		{
			tooltipsByLabel.insert ( field->label, tooltip_text ( field->description ) );

			++rows;
		}
	}

	const QList<QLabel*> labels = row_labels ( dialog );

	QCOMPARE ( labels.size (), rows );

	for ( QLabel* const label : labels )
	{
		const QString tooltip = label->toolTip ();

		QVERIFY2 ( !tooltip.isEmpty (),                                       qPrintable ( label->text () ) );
		QVERIFY2 ( tooltipsByLabel.values ( label->text () ).contains ( tooltip ), qPrintable ( label->text () + QStringLiteral ( ": " ) + tooltip ) );
		QVERIFY2 ( tooltip != label->text (),                                 qPrintable ( label->text () ) );

		// The editor says the same, so the pointer can rest on either half of the row.

		QCOMPARE ( label->buddy ()->toolTip (), tooltip );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// SET-14a. The colour row speaks the theme showing when the dialog opened, and stores the Dark theme's colour.
//---------------------------------------------------------------------------------------------------------------------

void TestSettingsDialog::the_colour_row_speaks_the_theme_showing_data ()
{
	QTest::addColumn<bool>    ( "dark" );
	QTest::addColumn<QString> ( "stored" );
	QTest::addColumn<QString> ( "shown" );

	// The default is #808080 for Dark (AppConfig), which is #7F7F7F with its lightness inverted.

	QTest::newRow ( "default in dark" )  << true  << QString () << QStringLiteral ( "#808080" );
	QTest::newRow ( "default in light" ) << false << QString () << QStringLiteral ( "#7F7F7F" );
	QTest::newRow ( "stored in dark" )   << true  << QStringLiteral ( "#336699" ) << QStringLiteral ( "#336699" );
	QTest::newRow ( "stored in light" )  << false << QStringLiteral ( "#336699" ) << QStringLiteral ( "#6699CC" );
}

void TestSettingsDialog::the_colour_row_speaks_the_theme_showing ()
{
	QFETCH ( bool,    dark );
	QFETCH ( QString, stored );
	QFETCH ( QString, shown );

	ColourFixture fixture ( dark );

	if ( !stored.isEmpty () )
	{
		fixture.store.set_string ( settings_keys::CHANGE_MARK_COLOUR, stored );
	}

	SettingsDialog dialog ( settings_schema (), &fixture.store, &fixture.theme, nullptr, QIcon () );

	QLineEdit* const box = colour_box ( dialog );

	QVERIFY  ( box != nullptr );
	QCOMPARE ( box->text (), shown );
	QVERIFY  ( choose_button ( *box ) != nullptr );
}

void TestSettingsDialog::a_colour_typed_is_stored_as_the_dark_theme_s ()
{
	// In Light: #1A1A1A typed is the colour the user wants to SEE in Light, so what is stored is its inverse, #E5E5E5.

	{
		ColourFixture fixture ( false );

		SettingsDialog dialog ( settings_schema (), &fixture.store, &fixture.theme, nullptr, QIcon () );

		colour_box ( dialog )->setText ( QStringLiteral ( "#1a1a1a" ) );

		press_ok ( dialog );

		QCOMPARE ( fixture.store.value_string ( settings_keys::CHANGE_MARK_COLOUR, QString () ), QStringLiteral ( "#E5E5E5" ) );
	}

	// In Dark: stored as typed -- the short form expanded, in the one written form.

	{
		ColourFixture fixture ( true );

		SettingsDialog dialog ( settings_schema (), &fixture.store, &fixture.theme, nullptr, QIcon () );

		colour_box ( dialog )->setText ( QStringLiteral ( "abc" ) );

		press_ok ( dialog );

		QCOMPARE ( fixture.store.value_string ( settings_keys::CHANGE_MARK_COLOUR, QString () ), QStringLiteral ( "#AABBCC" ) );
	}
}

void TestSettingsDialog::an_incomplete_colour_is_never_stored_and_reverts ()
{
	ColourFixture fixture ( true );

	fixture.store.set_string ( settings_keys::CHANGE_MARK_COLOUR, QStringLiteral ( "#336699" ) );

	SettingsDialog dialog ( settings_schema (), &fixture.store, &fixture.theme, nullptr, QIcon () );

	QLineEdit* const box = colour_box ( dialog );

	// What cannot become a colour cannot be typed at all.

	QString rejected = QStringLiteral ( "#12zz" );
	int     position = rejected.size ();

	QCOMPARE ( box->validator ()->validate ( rejected, position ), QValidator::Invalid );

	// What can but is not one yet is allowed while typing, and goes back to the last complete value on leaving.

	box->setText ( QStringLiteral ( "#12" ) );

	QCOMPARE ( box->text (), QStringLiteral ( "#12" ) );

	emit box->editingFinished ();

	QCOMPARE ( box->text (), QStringLiteral ( "#336699" ) );

	// A complete value in another form is written back in the one form.

	box->setText ( QStringLiteral ( "#abc" ) );

	emit box->editingFinished ();

	QCOMPARE ( box->text (), QStringLiteral ( "#AABBCC" ) );

	// And an incomplete value left in the box at OK applies the last complete one.

	box->setText ( QStringLiteral ( "#4" ) );

	press_ok ( dialog );

	QCOMPARE ( fixture.store.value_string ( settings_keys::CHANGE_MARK_COLOUR, QString () ), QStringLiteral ( "#AABBCC" ) );
}

void TestSettingsDialog::choose_opens_the_picker_on_the_colour_shown ()
{
	ColourFixture    fixture ( false );
	ColourPickerFake picker;

	SettingsDialog dialog ( settings_schema (), &fixture.store, &fixture.theme, &picker, QIcon () );

	QLineEdit*   const box    = colour_box ( dialog );
	QPushButton* const choose = choose_button ( *box );

	QVERIFY ( choose != nullptr );

	// Cancelled: nothing changes.

	choose->click ();

	QCOMPARE ( picker.calls, 1 );
	QCOMPARE ( picker.initialSeen, QColor ( 0x7F, 0x7F, 0x7F ) );           // The colour the box shows, in Light.
	QCOMPARE ( picker.titleSeen, QStringLiteral ( "Unsaved change color" ) );
	QCOMPARE ( box->text (), QStringLiteral ( "#7F7F7F" ) );

	// Picked: the box shows it, and OK stores the Dark theme's colour -- its lightness inverse.

	picker.answer = QColor ( 0x11, 0x22, 0x33 );

	choose->click ();

	QCOMPARE ( box->text (), QStringLiteral ( "#112233" ) );

	press_ok ( dialog );

	QCOMPARE ( fixture.store.value_string ( settings_keys::CHANGE_MARK_COLOUR, QString () ), QStringLiteral ( "#CCDDEE" ) );
}

#include "tst_settings_dialog.moc"

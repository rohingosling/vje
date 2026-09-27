//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Rendered-pixel coverage for TreeNodeDelegate, TREE-10's unsaved-change dot: that it is painted at all, where it is
//   marked and nowhere else, in the accent on an ordinary row and in the row's text colour on a selected one, on BOTH
//   themes; that SET-14's File node only scope leaves the root's dot alone; that a clean document paints none; and that
//   a dot appearing does not change the column's width. Every claim is read back from the rendered viewport, with the
//   reference colours taken from the palette the application installs (lesson Q12).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "models/JsonTreeModel.hpp"
#include "services/ThemeService.hpp"
#include "style/FocusHighlight.hpp"
#include "views/TreeNodeDelegate.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTreeView>

#include <cstdlib>
#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({ "name": "vje", "meta": { "version": "2.0.0", "stable": true } })";

	// A QTreeView that will say what option it paints a row with -- initViewItemOption is protected -- so the case
	// asks the delegate where the dot goes with exactly what the paint was given.

	class ProbeTreeView : public QTreeView
	{
	public:

		QStyleOptionViewItem item_option ( const QModelIndex& index )
		{
			QStyleOptionViewItem option;

			initViewItemOption ( &option );

			option.rect = visualRect ( index );

			if ( selectionModel ()->isSelected ( index ) )
			{
				option.state |= QStyle::State_Selected;
			}

			return option;
		}
	};

	bool near ( const QColor& actual, const QColor& expected )
	{
		const int tolerance = 8;

		return ( std::abs ( actual.red ()   - expected.red () )   <= tolerance ) &&
		       ( std::abs ( actual.green () - expected.green () ) <= tolerance ) &&
		       ( std::abs ( actual.blue ()  - expected.blue () )  <= tolerance );
	}
}

class TestTreeNodeDelegate : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void the_dot_is_painted_on_the_marked_rows_and_no_others_data ();
	void the_dot_is_painted_on_the_marked_rows_and_no_others ();
	void a_selected_row_s_dot_is_its_text_colour_data ();
	void a_selected_row_s_dot_is_its_text_colour ();
	void the_file_node_only_scope_paints_the_root_alone ();
	void a_clean_document_paints_no_dot ();
	void a_dot_appearing_does_not_widen_the_column ();
	void an_unfocused_tree_keeps_the_accent ();
	void a_set_colour_paints_every_unselected_dot ();

private:

	void build_view ();

	QColor dot_colour ( const QString& pointerText );        // The colour at the middle of that row's dot rectangle.
	QColor row_colour ( const QString& pointerText );        // The row's own background, in the gap before the dot.

	bool has_dot ( const QString& pointerText, const QColor& colour );
	bool has_no_dot ( const QString& pointerText );
	QColor accent () const;

	QModelIndex index_of ( const QString& pointerText ) const;

	std::unique_ptr<QTemporaryDir>    temporaryDirectory;
	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<ThemeService>     theme;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<JsonTreeModel>    model;
	std::unique_ptr<QWidget>          window;                     // The tree and a sibling, as the workspace has.
	ProbeTreeView*                    view      = nullptr;        // Owned by the window.
	QLineEdit*                        elsewhere = nullptr;        // Owned by the window; takes the keyboard off the tree.
	TreeNodeDelegate*                 delegate  = nullptr;        // Owned by the view.
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestTreeNodeDelegate::init ()
{
	temporaryDirectory = std::make_unique<QTemporaryDir> ();

	settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
	theme    = std::make_unique<ThemeService>  ( settings.get () );

	theme->apply ();
	theme->set_theme ( Theme::Light );

	document = std::make_unique<JsonDocument>   ();
	undo     = std::make_unique<UndoController> ( document.get () );
	model    = std::make_unique<JsonTreeModel>  ( document.get (), nullptr );

	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( SAMPLE_DOCUMENT ) );

	document->set_root ( std::move ( result.root ) );
}

void TestTreeNodeDelegate::cleanup ()
{
	// Reverse construction order (lesson Q1).

	window.reset   ();
	model.reset    ();
	undo.reset     ();
	document.reset ();
	theme.reset    ();
	settings.reset ();

	temporaryDirectory.reset ();
}

void TestTreeNodeDelegate::build_view ()
{
	// Built AFTER the theme is chosen: an application palette set later does not reach a widget already constructed
	// (lesson Q9). Configured as TreeViewPane configures the real one, where it matters to where a dot lands.

	window = std::make_unique<QWidget> ();

	QHBoxLayout* const layout = new QHBoxLayout ( window.get () );

	view      = new ProbeTreeView ();
	elsewhere = new QLineEdit ();

	layout->addWidget ( view, 1 );
	layout->addWidget ( elsewhere );

	delegate = new TreeNodeDelegate ( view );

	view->setItemDelegate      ( delegate );
	view->setModel             ( model.get () );
	view->setHeaderHidden      ( true );
	view->setUniformRowHeights ( true );
	view->header ()->setStretchLastSection ( false );
	view->header ()->setSectionResizeMode ( 0, QHeaderView::ResizeToContents );
	view->expandAll            ();

	window->resize ( 500, 300 );
	window->show   ();

	QVERIFY ( QTest::qWaitForWindowExposed ( window.get () ) );
}

QModelIndex TestTreeNodeDelegate::index_of ( const QString& pointerText ) const
{
	return model->index_for_pointer ( JsonPointer::parse ( pointerText ) );
}

QColor TestTreeNodeDelegate::dot_colour ( const QString& pointerText )
{
	QCoreApplication::processEvents ();

	const QModelIndex index = index_of ( pointerText );
	const QRectF      dot   = delegate->change_mark_rect ( view->item_option ( index ), index );
	const QImage      image = view->viewport ()->grab ().toImage ();

	// The pixel at the dot's middle is inside the disc whatever the antialiasing does to its edge.

	return QColor ( image.pixel ( static_cast<int> ( dot.center ().x () - 0.5 ), static_cast<int> ( dot.center ().y () - 0.5 ) ) );
}

QColor TestTreeNodeDelegate::row_colour ( const QString& pointerText )
{
	QCoreApplication::processEvents ();

	const QModelIndex index = index_of ( pointerText );
	const QRectF      dot   = delegate->change_mark_rect ( view->item_option ( index ), index );
	const QImage      image = view->viewport ()->grab ().toImage ();

	// Halfway across the gap between the label and the dot: the row as painted there, which is the Base colour on most
	// rows but not all -- Fusion fills the CURRENT row with a translucent focus rectangle -- so "no dot" is compared
	// with the row itself rather than with a palette role.

	const int gapMiddle = static_cast<int> ( dot.left () ) - ( config::tree::CHANGE_MARK_GAP / 2 );

	return QColor ( image.pixel ( gapMiddle, static_cast<int> ( dot.center ().y () - 0.5 ) ) );
}

bool TestTreeNodeDelegate::has_dot ( const QString& pointerText, const QColor& colour )
{
	const QColor dot = dot_colour ( pointerText );

	if ( !near ( dot, colour ) )
	{
		qWarning ( "%s: dot %s, expected %s", qPrintable ( pointerText ), qPrintable ( dot.name () ), qPrintable ( colour.name () ) );

		return false;
	}

	return true;
}

bool TestTreeNodeDelegate::has_no_dot ( const QString& pointerText )
{
	const QColor dot = dot_colour ( pointerText );
	const QColor row = row_colour ( pointerText );

	if ( !near ( dot, row ) )
	{
		qWarning ( "%s: %s where the dot would be, on a row of %s", qPrintable ( pointerText ), qPrintable ( dot.name () ), qPrintable ( row.name () ) );

		return false;
	}

	return true;
}

QColor TestTreeNodeDelegate::accent () const
{
	// VJE's accent is its selection blue -- the focused bar, as the THEME states it, read from the application palette
	// rather than the view's, which FocusHighlight rewrites. Deliberately not QPalette::Accent: asking the role the
	// delegate paints with would agree with the delegate whatever colour that role had been left holding.

	return QApplication::palette ().color ( QPalette::Active, QPalette::Highlight );
}

//---------------------------------------------------------------------------------------------------------------------
// Test Cases
//---------------------------------------------------------------------------------------------------------------------

void TestTreeNodeDelegate::the_dot_is_painted_on_the_marked_rows_and_no_others_data ()
{
	QTest::addColumn<bool> ( "dark" );

	QTest::newRow ( "light" ) << false;
	QTest::newRow ( "dark" )  << true;
}

void TestTreeNodeDelegate::the_dot_is_painted_on_the_marked_rows_and_no_others ()
{
	QFETCH ( bool, dark );

	theme->set_theme ( dark ? Theme::Dark : Theme::Light );

	build_view ();

	// The accent must stand off the row's background, or a correct paint would still be invisible.

	QVERIFY ( !near ( accent (), view->palette ().color ( QPalette::Base ) ) );

	// Nothing edited: no dot anywhere.

	for ( const QString& row : { QString (), QStringLiteral ( "/name" ), QStringLiteral ( "/meta" ), QStringLiteral ( "/meta/version" ) } )
	{
		QVERIFY2 ( has_no_dot ( row ), qPrintable ( row ) );
	}

	QCOMPARE ( undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) ), EditOutcome::Applied );

	// The edited row and its two ancestors, in the accent; the untouched siblings, still nothing.

	for ( const QString& row : { QString (), QStringLiteral ( "/meta" ), QStringLiteral ( "/meta/version" ) } )
	{
		QVERIFY2 ( has_dot ( row, accent () ), qPrintable ( row ) );
	}

	for ( const QString& row : { QStringLiteral ( "/name" ), QStringLiteral ( "/meta/stable" ) } )
	{
		QVERIFY2 ( has_no_dot ( row ), qPrintable ( row ) );
	}
}

void TestTreeNodeDelegate::a_selected_row_s_dot_is_its_text_colour_data ()
{
	QTest::addColumn<bool> ( "dark" );

	QTest::newRow ( "light" ) << false;
	QTest::newRow ( "dark" )  << true;
}

void TestTreeNodeDelegate::a_selected_row_s_dot_is_its_text_colour ()
{
	QFETCH ( bool, dark );

	theme->set_theme ( dark ? Theme::Dark : Theme::Light );

	build_view ();

	window->activateWindow ();
	view->setFocus         ();

	QVERIFY ( QTest::qWaitForWindowActive ( window.get () ) );

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	view->setCurrentIndex ( index_of ( QStringLiteral ( "/meta/version" ) ) );

	// The focused selection bar IS the accent, so a dot in the accent would vanish into it. It is drawn in the colour
	// the palette already guarantees reads on the bar: the row's own selected-text colour.

	const QColor bar      = view->palette ().color ( QPalette::Active, QPalette::Highlight );
	const QColor selected = view->palette ().color ( QPalette::Active, QPalette::HighlightedText );

	QVERIFY ( near ( bar, accent () ) );                                   // The hazard is real in this palette.

	QVERIFY ( has_dot ( QStringLiteral ( "/meta/version" ), selected ) );
	QVERIFY ( !near ( selected, bar ) );

	// And the unselected rows keep the accent.

	QVERIFY ( has_dot ( QStringLiteral ( "/meta" ), accent () ) );
}

void TestTreeNodeDelegate::the_file_node_only_scope_paints_the_root_alone ()
{
	model->set_change_mark_scope ( config::tree::ChangeMarkScope::FileNodeOnly );

	build_view ();

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QVERIFY ( has_dot    ( QString (), accent () ) );
	QVERIFY ( has_no_dot ( QStringLiteral ( "/meta" ) ) );
	QVERIFY ( has_no_dot ( QStringLiteral ( "/meta/version" ) ) );

	// Switched back with the view on screen: the chain comes back without a reload.

	model->set_change_mark_scope ( config::tree::ChangeMarkScope::ChangedNodesAndAncestors );

	QVERIFY ( has_dot ( QStringLiteral ( "/meta" ),         accent () ) );
	QVERIFY ( has_dot ( QStringLiteral ( "/meta/version" ), accent () ) );
}

void TestTreeNodeDelegate::a_clean_document_paints_no_dot ()
{
	build_view ();

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QVERIFY ( has_dot ( QStringLiteral ( "/meta/version" ), accent () ) );

	// Undo back to the loaded state, which is the saved point.

	undo->undo ();

	QVERIFY ( !document->is_dirty () );

	for ( const QString& row : { QString (), QStringLiteral ( "/meta" ), QStringLiteral ( "/meta/version" ) } )
	{
		QVERIFY2 ( has_no_dot ( row ), qPrintable ( row ) );
	}
}

void TestTreeNodeDelegate::a_dot_appearing_does_not_widen_the_column ()
{
	build_view ();

	QCoreApplication::processEvents ();

	const int widthBefore = view->header ()->sectionSize ( 0 );

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QCoreApplication::processEvents ();

	// The room was already there: every row's size hint reserves the gap and the dot, marked or not. And the dot fits
	// inside the column rather than hanging off its edge.

	QCOMPARE ( view->header ()->sectionSize ( 0 ), widthBefore );

	const QModelIndex index = index_of ( QStringLiteral ( "/meta/version" ) );

	QVERIFY ( delegate->change_mark_rect ( view->item_option ( index ), index ).right () <= view->visualRect ( index ).right () + 1 );
}

void TestTreeNodeDelegate::an_unfocused_tree_keeps_the_accent ()
{
	// The real tree runs under FocusHighlight, which rewrites the view's Highlight to the muted bar whenever the tree
	// loses the keyboard (STYLE-12). The dot is the ACCENT, not the highlight, so it must not mute with the bar -- which
	// is why ThemeService states the accent rather than leaving Qt to derive it from Highlight.

	build_view ();

	FocusHighlight::install ( view );

	// The keyboard goes to the tree's sibling, so the tree is unfocused while the window stays active -- another pane
	// holding the keyboard, as the editor pane does.

	window->activateWindow ();

	QVERIFY ( QTest::qWaitForWindowActive ( window.get () ) );

	elsewhere->setFocus ();

	QTRY_VERIFY ( elsewhere->hasFocus () );

	// The theme's accent -- what the dot must be whatever the view's own palette did.

	const QColor themeAccent = accent ();
	const QColor mutedBar    = QApplication::palette ().color ( QPalette::Inactive, QPalette::Highlight );

	QVERIFY ( !near ( themeAccent, mutedBar ) );                            // The two are told apart in this palette.

	// FocusHighlight has muted the view's bar -- on a deferred refresh, so waited for: the hazard is present.

	QTRY_VERIFY ( near ( view->palette ().color ( QPalette::Active, QPalette::Highlight ), mutedBar ) );

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QVERIFY ( has_dot ( QStringLiteral ( "/meta" ), themeAccent ) );
}

void TestTreeNodeDelegate::a_set_colour_paints_every_unselected_dot ()
{
	// SET-14a. The window pushes the colour for the theme in effect; the delegate paints it on every unselected marked
	// row, and a selected row keeps its own text colour whatever the setting says (TREE-10).

	build_view ();

	window->activateWindow ();
	view->setFocus         ();

	QVERIFY ( QTest::qWaitForWindowActive ( window.get () ) );

	const QColor chosen ( 0xC0, 0x40, 0x80 );

	delegate->set_mark_colour ( chosen );

	undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/meta/version" ) ), QStringLiteral ( "3" ) );

	QVERIFY ( has_dot ( QString (),                chosen ) );
	QVERIFY ( has_dot ( QStringLiteral ( "/meta" ), chosen ) );

	view->setCurrentIndex ( index_of ( QStringLiteral ( "/meta/version" ) ) );

	QVERIFY ( has_dot ( QStringLiteral ( "/meta/version" ), view->palette ().color ( QPalette::Active, QPalette::HighlightedText ) ) );
}

QTEST_MAIN ( TestTreeNodeDelegate )

#include "tst_tree_node_delegate.moc"

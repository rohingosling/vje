//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   toolbar_catalogue -- which commands MAY sit on the toolbar, which ones do by default, and how a stored layout is
//   made safe to use (SET-04, architecture.md section 8.1).
//
//   WHY AN ORDERED LIST RATHER THAN A SET OF BOOLEANS. SET-04 gives the user the toolbar's membership AND its order, so
//   the stored setting is one ordered list of entry names. Membership is then "is the name in the list" and order is
//   "where in the list" -- one representation answering both questions, which is what stops the plan, the dialog, and
//   the persisted form from each holding half an opinion. The previous per-button visibility keys are migrated once and
//   removed (see stored_toolbar_layout).
//
//   THE CATALOGUE AND THE DEFAULT LAYOUT ARE TWO STATEMENTS, DELIBERATELY. The catalogue is the eligibility set; the
//   default layout is a curated subset of it in a chosen order. Collapsing the two -- "the catalogue IS the default, in
//   order" -- would mean every command the user can add must also ship on the bar, which is the opposite of what the
//   feature is for. What keeps them honest is that both are spelled from toolbar_names, and a test asserts every default
//   entry resolves in the catalogue.
//
//   THE NAMES ARE PERSISTED, so they are untranslated, independent of the command's label, and must not change: a
//   renamed entry silently drops out of every existing user's layout. The fifteen that predate 2026-07-27 are spelled
//   exactly as the superseded toolbar.visible.* keys spelled them, which is what makes the migration a rename-free
//   lookup.
//
//   ELIGIBILITY IS BOUNDED BY THE ICON SET. The toolbar is Qt::ToolButtonIconOnly, so a command with no glyph could only
//   render as a blank square; the catalogue is therefore exactly the commands MainWindow::apply_action_icons assigns a
//   glyph to. That is why the three array transforms gained glyphs in this phase and why Exit, the theme radio items,
//   and the Help links stay out.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

class QAction;

namespace vje
{
	class SettingsStore;

	//-----------------------------------------------------------------------------------------------------------------
	// One toolbar-eligible command: the action that runs it, and the stable name it persists under.
	//-----------------------------------------------------------------------------------------------------------------

	struct ToolbarCommand
	{
		QAction* action = nullptr;
		QString  name;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Layout entry names. SEPARATOR is reserved -- it is the one entry that is repeatable, carries no command, and may
	// appear any number of times in a layout.
	//-----------------------------------------------------------------------------------------------------------------

	namespace toolbar_names
	{
		inline const QString SEPARATOR = QStringLiteral ( "separator" );

		// -- File.

		inline const QString NEW        = QStringLiteral ( "new" );
		inline const QString OPEN       = QStringLiteral ( "open" );
		inline const QString CLOSE      = QStringLiteral ( "close" );
		inline const QString SAVE       = QStringLiteral ( "save" );
		inline const QString SAVE_AS    = QStringLiteral ( "saveAs" );
		inline const QString PAGE_SETUP = QStringLiteral ( "pageSetup" );
		inline const QString PRINT      = QStringLiteral ( "print" );
		inline const QString SETTINGS   = QStringLiteral ( "settings" );

		// -- Edit.

		inline const QString FIND         = QStringLiteral ( "find" );
		inline const QString GO_TO        = QStringLiteral ( "goTo" );
		inline const QString COPY_POINTER = QStringLiteral ( "copyPointer" );
		inline const QString UNDO         = QStringLiteral ( "undo" );
		inline const QString REDO  = QStringLiteral ( "redo" );
		inline const QString CUT   = QStringLiteral ( "cut" );
		inline const QString COPY  = QStringLiteral ( "copy" );
		inline const QString PASTE = QStringLiteral ( "paste" );

		// -- Document: the six Add commands.

		inline const QString ADD_OBJECT  = QStringLiteral ( "addObject" );
		inline const QString ADD_ARRAY   = QStringLiteral ( "addArray" );
		inline const QString ADD_STRING  = QStringLiteral ( "addString" );
		inline const QString ADD_NUMBER  = QStringLiteral ( "addNumber" );
		inline const QString ADD_BOOLEAN = QStringLiteral ( "addBoolean" );
		inline const QString ADD_NULL    = QStringLiteral ( "addNull" );

		// -- Document: node operations.

		inline const QString RENAME_KEY     = QStringLiteral ( "renameKey" );
		inline const QString DUPLICATE_NODE = QStringLiteral ( "duplicateNode" );
		inline const QString DELETE_NODE    = QStringLiteral ( "deleteNode" );
		inline const QString MOVE_UP        = QStringLiteral ( "moveUp" );
		inline const QString MOVE_DOWN      = QStringLiteral ( "moveDown" );

		// -- Document: the array transforms (EDIT-11..13).

		inline const QString NORMALIZE_ARRAY  = QStringLiteral ( "normalizeArray" );
		inline const QString ARRAY_TO_OBJECTS = QStringLiteral ( "arrayToObjects" );
		inline const QString OBJECTS_TO_ARRAY = QStringLiteral ( "objectsToArray" );

		// -- View.

		inline const QString EXPAND_ALL   = QStringLiteral ( "expandAll" );
		inline const QString COLLAPSE_ALL = QStringLiteral ( "collapseAll" );

		// -- Help.

		inline const QString ABOUT = QStringLiteral ( "about" );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The shipped layout (section 2.4), as entry names. Every entry resolves in the catalogue MainWindow builds;
	// tst_toolbar_catalogue asserts it.
	//-----------------------------------------------------------------------------------------------------------------

	QStringList default_toolbar_layout ();

	//-----------------------------------------------------------------------------------------------------------------
	// A stored layout made safe to render and to show in the dialog: an entry naming no catalogue command is dropped
	// (a command this build does not have, or a settings file from a newer one), and a command appearing twice keeps
	// only its first occurrence.
	//
	// SEPARATORS ARE LEFT EXACTLY WHERE THEY ARE, including a leading, trailing, or doubled one. Collapsing them is
	// toolbar_plan's job at RENDER time, because the layout the user arranged is what the dialog has to show them
	// again -- normalizing on write would silently edit the arrangement they are looking at.
	//-----------------------------------------------------------------------------------------------------------------

	QStringList normalized_toolbar_layout ( const QStringList& layout, const std::vector<ToolbarCommand>& catalogue );

	//-----------------------------------------------------------------------------------------------------------------
	// The layout to start from (SET-04). An absent key yields the default layout -- which is NOT the same as a stored
	// empty list, a legal layout meaning "no buttons", hence the contains() test rather than an isEmpty() one.
	//
	// A settings file written before 2026-07-27 carries one visibility key per button instead. Those are read once into
	// a layout -- the default order minus the unchecked buttons -- which is then written under the new key and the old
	// keys removed, so an existing customization survives the upgrade exactly once and leaves nothing behind. Passing a
	// non-const store is what allows that; a read-only caller may pass nullptr for settings and get the default.
	//-----------------------------------------------------------------------------------------------------------------

	QStringList stored_toolbar_layout ( SettingsStore* settings, const std::vector<ToolbarCommand>& catalogue );

	//-----------------------------------------------------------------------------------------------------------------
	// A command's display label: its own text with the mnemonic marker and the ellipsis stripped, so the toolbar's
	// tooltips and the Settings dialog's two lists read the command the same way. Stated once here rather than at the
	// three call sites that need it.
	//-----------------------------------------------------------------------------------------------------------------

	QString toolbar_command_label ( const QAction* action );
}

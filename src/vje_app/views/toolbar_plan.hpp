//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   toolbar_plan -- what the toolbar should render, given the user's layout (SET-04).
//
//   WHY A HIDDEN BUTTON IS ONE THE TOOLBAR DOES NOT CONTAIN. Neither obvious route works. Hiding the QAction is not
//   available: the menu bar and the context menus share the very same action, so hiding it would remove the command from
//   the whole application rather than from the toolbar. Hiding the tool BUTTON does not hold either -- QToolBar's layout
//   drives each button's visibility from its action's on every relayout, so a directly hidden button quietly comes back
//   (lessons-learned.md Q23). What is left, and what is also the simplest thing to reason about, is CONTENT: the toolbar
//   is rebuilt to hold exactly the planned items. That was true of the original per-button SET-04 and is unchanged; what
//   changed on 2026-07-27 is only the INPUT -- an ordered layout instead of a predicate over a fixed order.
//
//   SEPARATOR COLLAPSING IS NOW LOAD-BEARING RATHER THAN DEFENSIVE. Under the old model a stray rule could only arise
//   from a whole group being switched off; a user arranging their own layout can author a leading, a trailing, or three
//   consecutive separators directly. The rule is unchanged -- a separator is emitted only once a visible button follows
//   it -- but the layout is deliberately NOT normalized on write (views/toolbar_catalogue), so this is the only place
//   the tidying happens and every stray-rule shape has to survive it.
//
//   The plan is TOTAL: an entry naming no catalogue command is dropped rather than resolved to a null button, so a
//   settings file from a newer build renders what this one understands and nothing else.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/toolbar_catalogue.hpp"

#include <QStringList>

#include <vector>

class QAction;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One item the toolbar should realize: a command, or a separator.
	//-----------------------------------------------------------------------------------------------------------------

	struct ToolbarItem
	{
		QAction* action    = nullptr;                          // Null when separator is true.
		bool     separator = false;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The plan: each layout entry resolved against the catalogue, with the stray rules collapsed.
	//-----------------------------------------------------------------------------------------------------------------

	std::vector<ToolbarItem> toolbar_plan ( const QStringList& layout, const std::vector<ToolbarCommand>& catalogue );
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   NodeContextActions implementation -- the shared node context menu layout. See the header for why it is shared.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/NodeContextActions.hpp"

#include <QAction>
#include <QMenu>

#include <utility>
#include <vector>

namespace vje
{
	namespace
	{
		// The six typed add commands, in the Document menu's order (EDIT-03 / 04). One table, so the Add Child and Add
		// Sibling submenus offer exactly the same set in the same order.

		const std::vector<std::pair<JsonKind, const char*>>& add_type_table ()
		{
			static const std::vector<std::pair<JsonKind, const char*>> table
			{
				{ JsonKind::Object,  QT_TRANSLATE_NOOP ( "NodeContextActions", "Add &Object"  ) },
				{ JsonKind::Array,   QT_TRANSLATE_NOOP ( "NodeContextActions", "Add &Array"   ) },
				{ JsonKind::String,  QT_TRANSLATE_NOOP ( "NodeContextActions", "Add &String"  ) },
				{ JsonKind::Number,  QT_TRANSLATE_NOOP ( "NodeContextActions", "Add &Number"  ) },
				{ JsonKind::Boolean, QT_TRANSLATE_NOOP ( "NodeContextActions", "Add &Boolean" ) },
				{ JsonKind::Null,    QT_TRANSLATE_NOOP ( "NodeContextActions", "Add N&ull"    ) }
			};

			return table;
		}

		// Fill a placement submenu with the six type items. Enabled as a whole by the predicate, evaluated now (the
		// menu is opening), so it tracks the live selection with no persistent state.

		void build_add_submenu
		(
			QMenu*                                  submenu,
			const std::function<void ( JsonKind )>& callback,
			const std::function<bool ()>&           canPlace
		)
		{
			submenu->setEnabled ( callback && ( !canPlace || canPlace () ) );

			populate_add_type_menu ( submenu, callback );
		}
	}

	void populate_add_type_menu ( QMenu* menu, const std::function<void ( JsonKind )>& callback )
	{
		for ( const auto& [ kind, label ] : add_type_table () )
		{
			QAction* const item = menu->addAction ( QObject::tr ( label ) );

			const JsonKind itemKind = kind;

			QObject::connect ( item, &QAction::triggered, menu, [ callback, itemKind ] () { if ( callback ) { callback ( itemKind ); } } );
		}
	}

	void populate_node_context_menu ( QMenu* menu, const NodeContextActions& actions )
	{
		auto add_action = [ menu ] ( QAction* action )
		{
			if ( action != nullptr )
			{
				menu->addAction ( action );
			}
		};

		add_action ( actions.cut );
		add_action ( actions.copy );
		add_action ( actions.paste );
		add_action ( actions.copyPointer );

		menu->addSeparator ();

		add_action ( actions.duplicate );
		add_action ( actions.remove );
		add_action ( actions.rename );

		menu->addSeparator ();

		// EDIT-04: the Add Child / Add Sibling submenus, each the six typed adds relative to the selection. The
		// callbacks and their gating predicates come from MainWindow, which owns the command logic.

		QMenu* const addChildMenu   = menu->addMenu ( QObject::tr ( "Add &Child" ) );
		QMenu* const addSiblingMenu = menu->addMenu ( QObject::tr ( "Add &Sibling" ) );

		build_add_submenu ( addChildMenu,   actions.addChild,   actions.canAddChild );
		build_add_submenu ( addSiblingMenu, actions.addSibling, actions.canAddSibling );

		menu->addSeparator ();

		add_action ( actions.moveUp );
		add_action ( actions.moveDown );

		menu->addSeparator ();

		// The array transforms (EDIT-11..13), in this order by request: the two Convert commands as they were set on
		// 2026-07-24 -- the object transform first, then the array one -- and Normalize Array Elements below them.
		// Disabled-not-hidden like everything else: each applies to exactly one shape of container, and the centralized
		// enablement already says which.

		add_action ( actions.objectsToArray );
		add_action ( actions.arrayToObjects );
		add_action ( actions.normalizeArray );
	}
}

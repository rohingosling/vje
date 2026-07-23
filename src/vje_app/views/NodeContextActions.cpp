//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

namespace vje
{
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

		menu->addSeparator ();

		add_action ( actions.duplicate );
		add_action ( actions.remove );
		add_action ( actions.rename );

		menu->addSeparator ();

		QMenu* const addChildMenu   = menu->addMenu ( QObject::tr ( "Add &Child" ) );
		QMenu* const addSiblingMenu = menu->addMenu ( QObject::tr ( "Add &Sibling" ) );

		addChildMenu  ->setEnabled ( false );
		addSiblingMenu->setEnabled ( false );

		menu->addSeparator ();

		add_action ( actions.moveUp );
		add_action ( actions.moveDown );
	}
}

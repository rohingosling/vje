//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   toolbar_plan implementation. See the header for why visibility is content rather than a hidden widget, and why the
//   separator collapse lives here rather than in the stored layout.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/toolbar_plan.hpp"

#include <QAction>

namespace vje
{
	namespace
	{
		QAction* resolve ( const std::vector<ToolbarCommand>& catalogue, const QString& name )
		{
			for ( const ToolbarCommand& command : catalogue )
			{
				if ( command.name == name )
				{
					return command.action;
				}
			}

			return nullptr;
		}
	}

	std::vector<ToolbarItem> toolbar_plan ( const QStringList& layout, const std::vector<ToolbarCommand>& catalogue )
	{
		std::vector<ToolbarItem> plan;

		// A separator is only ever emitted once a visible button FOLLOWS it, which is what collapses a leading rule, a
		// trailing one, and any run of consecutive rules -- with one flag rather than a case per shape.

		bool anyButtonSoFar   = false;
		bool separatorPending = false;

		for ( const QString& entry : layout )
		{
			if ( entry == toolbar_names::SEPARATOR )
			{
				separatorPending = anyButtonSoFar;

				continue;
			}

			QAction* const action = resolve ( catalogue, entry );

			if ( action == nullptr )
			{
				continue;
			}

			if ( separatorPending )
			{
				ToolbarItem separator;

				separator.separator = true;

				plan.push_back ( separator );

				separatorPending = false;
			}

			ToolbarItem button;

			button.action = action;

			plan.push_back ( button );

			anyButtonSoFar = true;
		}

		return plan;
	}
}

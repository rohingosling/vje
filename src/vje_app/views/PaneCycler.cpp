//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   PaneCycler implementation. See the header for what Tab is deliberately not taken from.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/PaneCycler.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	PaneCycler::PaneCycler ( QObject* parent )
		: QObject ( parent )
	{
		// An APPLICATION filter, because the key has to be caught before the focused widget decides what Tab means to
		// it. A filter on the panes would never see it: an event filter only observes events sent to the object it is
		// installed on, and a key press goes to the focus widget deep inside.
		//
		// The breadth is paid back in eventFilter, which declines everything that is not a bare Tab inside a
		// registered pane -- see the header.

		qApp->installEventFilter ( this );
	}

	//=================================================================================================================
	// Registry
	//=================================================================================================================

	void PaneCycler::add_pane
	(
		QWidget*               root,
		std::function<void ()> giveFocus,
		std::function<bool ()> claimsTab
	)
	{
		if ( ( root == nullptr ) || !giveFocus )
		{
			return;
		}

		panes.append ( Pane { root, std::move ( giveFocus ), std::move ( claimsTab ) } );
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void PaneCycler::cycle ( bool forward )
	{
		if ( panes.isEmpty () )
		{
			return;
		}

		const int current = pane_index_of ( QApplication::focusWidget () );

		// Focus outside the ring (or nowhere at all) enters at the first pane going forward and the last going back,
		// so the very first Tab after a click on the menu bar still lands somewhere sensible.

		const int step = forward ? 1 : ( panes.size () - 1 );

		const int next = ( current < 0 ) ? ( forward ? 0 : ( panes.size () - 1 ) )
		                                 : ( ( current + step ) % panes.size () );

		panes[ next ].giveFocus ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	int PaneCycler::pane_index_of ( const QWidget* widget ) const
	{
		if ( widget == nullptr )
		{
			return -1;
		}

		for ( int index = 0; index < panes.size (); ++index )
		{
			const QWidget* const root = panes[ index ].root;

			if ( ( root == widget ) || root->isAncestorOf ( widget ) )
			{
				return index;
			}
		}

		return -1;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	bool PaneCycler::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( event->type () != QEvent::KeyPress )
		{
			return QObject::eventFilter ( watched, event );
		}

		const QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

		// Backtab is what Qt delivers for Shift+Tab; the Shift is already spent producing it, so the modifier test
		// below accepts it as bare.

		const bool isTab     = ( keyEvent->key () == Qt::Key_Tab );
		const bool isBackTab = ( keyEvent->key () == Qt::Key_Backtab );

		if ( !isTab && !isBackTab )
		{
			return QObject::eventFilter ( watched, event );
		}

		// Ctrl+Tab is the editor pane's view-tab cycle (VIEW-01) and every other modifier belongs to whoever bound it.

		if ( keyEvent->modifiers () & ~Qt::ShiftModifier )
		{
			return QObject::eventFilter ( watched, event );
		}

		QWidget* const focused = QApplication::focusWidget ();

		const int current = pane_index_of ( focused );

		if ( current < 0 )
		{
			return QObject::eventFilter ( watched, event );
		}

		// The two ways a pane keeps Tab for itself.

		if ( is_open_cell_editor ( focused ) )
		{
			return QObject::eventFilter ( watched, event );
		}

		const std::function<bool ()>& claimsTab = panes[ current ].claimsTab;

		if ( claimsTab && claimsTab () )
		{
			return QObject::eventFilter ( watched, event );
		}

		cycle ( isTab );

		return true;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool PaneCycler::is_open_cell_editor ( const QWidget* widget )
	{
		// A cell editor is parented into the item view's VIEWPORT, which is what separates it from the view itself and
		// from the view's other children (the scroll bars and the headers hang off the view, not the viewport). So the
		// question "is the user typing a value?" is answerable as "is the focus inside a grid's viewport, and not the
		// grid itself?" -- the grid is where Tab means the next pane, the open editor is where it means the next cell
		// (EDITOR-03).
		//
		// Asked this way rather than through QAbstractItemView::state(), which says EditingState directly but is
		// protected, and reaching it would mean subclassing every view in the workspace to ask one question.

		for ( const QWidget* ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget () )
		{
			const QAbstractItemView* const view = qobject_cast<const QAbstractItemView*> ( ancestor );

			if ( view != nullptr )
			{
				return ( view != widget ) && view->viewport ()->isAncestorOf ( widget );
			}
		}

		return false;
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ReorderTreeView implementation. See the header for why each override is here.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/ReorderTreeView.hpp"

#include <QDropEvent>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	ReorderTreeView::ReorderTreeView ( QWidget* parent )
		: QTreeView ( parent )
	{
	}

	//=================================================================================================================
	// QAbstractItemView
	//=================================================================================================================

	void ReorderTreeView::startDrag ( Qt::DropActions supportedActions )
	{
		Q_UNUSED ( supportedActions )

		// COPY AND ONLY COPY, whatever the caller asked for -- see the header. The base class does everything else
		// worth having (the drag pixmap rendered from the selected rows, the hot spot, the nested event loop), and its
		// one destructive branch is reached only by a MoveAction that can no longer be produced.

		QTreeView::startDrag ( Qt::CopyAction );
	}

	void ReorderTreeView::dropEvent ( QDropEvent* event )
	{
		// The housekeeping QAbstractItemView::dropEvent would have done. It is here rather than inherited because the
		// base is not called at all (see below), and without it a drag that reached the top or bottom edge leaves the
		// auto-scroll timer running after the button has been released.

		stopAutoScroll ();
		setState ( QAbstractItemView::NoState );
		viewport ()->update ();

		const QModelIndex index = indexAt ( event->position ().toPoint () );

		// Translate the indicator the USER WAS LOOKING AT into a position among the parent's children. Reading the
		// indicator rather than the mouse is the point: it is what the view drew, so what lands is what was shown.
		//
		// OnItem and OnViewport are not reachable here -- JsonTreeModel::canDropMimeData refuses both, so the drag
		// never reported a legal drop over them -- and they are handled rather than asserted because a drop is one
		// event arriving from the window system, which is the wrong place to be certain about anything.

		QModelIndex dropParent;
		int         dropRow = -1;

		switch ( dropIndicatorPosition () )
		{
			case QAbstractItemView::AboveItem:

				dropParent = index.parent ();
				dropRow    = index.row ();

				break;

			case QAbstractItemView::BelowItem:

				dropParent = index.parent ();
				dropRow    = index.row () + 1;

				break;

			case QAbstractItemView::OnItem:
			case QAbstractItemView::OnViewport:

				break;
		}

		if ( ( dropRow < 0 ) || !dropParent.isValid () )
		{
			event->ignore ();

			return;
		}

		emit nodes_dropped ( dropParent, dropRow );

		// Accepted so the drag ends cleanly, with IGNORE as the action so nothing downstream treats it as a move it
		// should finish for us. QTreeView::dropEvent is deliberately not called: it would ask the model to perform the
		// drop, and this model does not do edits.

		event->setDropAction ( Qt::IgnoreAction );
		event->accept ();
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ReorderTreeView -- the QTreeView the navigation pane uses, which can start an EDIT-10 reorder drag and never moves
//   anything itself.
//
//   IT EXISTS FOR TWO OVERRIDES, and both are about keeping the edit where edits belong.
//
//     startDrag  QAbstractItemView removes the dragged rows THROUGH THE MODEL when QDrag::exec comes back with
//                Qt::MoveAction -- the toolkit's own implementation of an internal move. Here the move is an undoable
//                command raised by MainWindow, so the toolkit doing half of it would tear the nodes out from under
//                the command and leave the undo stack describing an edit that had already partly happened. Running
//                the drag with Copy as its only supported action makes MoveAction unreachable, so that branch
//                provably cannot run -- which is worth more than checking what today's Qt does inside it, since the
//                check would have to be repeated at every upgrade. JsonTreeModel::supportedDropActions is the other
//                half of the same decision.
//
//     dropEvent  Turns the landing place into model terms -- the parent whose children are being reordered, and the
//                position the run goes to -- and publishes it. It decides nothing else: whether the drop is legal was
//                already settled by JsonTreeModel::canDropMimeData, which is what withheld the indicator while the
//                pointer was somewhere illegal.
//
//   WHY A SUBCLASS RATHER THAN AN EVENT FILTER. dropIndicatorPosition() is protected, and it is the only thing that
//   distinguishes "above this row" from "below" it and from "onto" it. A filter would have to recompute it from the
//   mouse position against the row rectangle -- a second copy of a rule the toolkit already applies, free to disagree
//   with the indicator the user is looking at.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QTreeView>

namespace vje
{
	//*****************************************************************************************************************
	// Class: ReorderTreeView
	//*****************************************************************************************************************

	class ReorderTreeView : public QTreeView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit ReorderTreeView ( QWidget* parent = nullptr );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A drop that Qt accepted, in model terms: the container whose children are being reordered, and the position
		// the dragged run is to land at -- immediately before the child currently at dropRow, with rowCount meaning
		// "after the last one".
		//
		// WHAT IS BEING DRAGGED IS NOT CARRIED, deliberately. It is the tree's selection, which the receiver already
		// holds; passing a second copy of it through the drag would let the two disagree about what moved.

		void nodes_dropped ( const QModelIndex& dropParent, int dropRow );

		//=============================================================================================================
		// QWidget / QAbstractItemView
		//=============================================================================================================

	protected:

		void startDrag ( Qt::DropActions supportedActions ) override;
		void dropEvent ( QDropEvent* event ) override;
	};
}

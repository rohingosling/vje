//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GridHeaderView -- the array table's header, for BOTH orientations (EDITOR-03's revised "the header is a control,
//   not a caption"). It carries three things the stock QHeaderView does not:
//
//     - EDITOR-16's SELECTED SECTION, painted from the shared HeaderSelection the controller owns.
//     - EDIT-15's SORT ZONE: a narrow full-height band at the right of each column header, marked with a triangle.
//     - The routing between the two: a press in the sort zone sorts, a click anywhere else on the section selects.
//
//   THE ZONE IS THE TARGET; THE TRIANGLE IS ONLY ITS MARK. The first build hit-tested the triangle itself, and a
//   9 x 5 target is one the user has to aim at -- every near miss selected the column instead of sorting it, which is
//   not a smaller version of the intended command but a different one. A header is therefore divided into two zones,
//   a wide one that selects and a narrow one that sorts, with a visible divider between them so the boundary can be
//   seen rather than discovered.
//
//   WHY IT IS PAINTED HERE RATHER THAN BY QHeaderView. Qt has a sort indicator of its own (setSortIndicatorShown),
//   and it is unusable for this: the whole section is its hit target, so the header cannot also select. EDITOR-03
//   states the resolution -- a header click selects and the sort lives in a control INSIDE the header -- which needs
//   a region small enough to miss and big enough to hit. sort_zone_rect defines that region ONCE and both the paint
//   and the hit test read it, so "a click outside the zone selects rather than sorts" is a property of a single rect
//   rather than an agreement between two functions that could drift apart.
//
//   WHY THE PRESS IS INTERCEPTED AND THE CLICK IS NOT. A press on the marker is consumed here, so QHeaderView never
//   sees it and never emits sectionClicked -- which is what keeps a sort from also selecting. Every other press is
//   handed straight to the base class, so section RESIZING by dragging a divider is untouched, and the resulting
//   sectionClicked becomes the selection. A resize drag emits no sectionClicked, so dragging a divider selects
//   nothing, which is the behaviour a spreadsheet has.
//
//   WHY THE SELECTION IS NOT QItemSelectionModel'S. The array table is SingleSelection / SelectItems and its unit is
//   a CELL; a row or column selection is a different question asked of the same grid, and EDITOR-16 requires exactly
//   one of the two to be live at a time. Holding it as explicit state makes that "exactly one" a field rather than a
//   predicate derived from what a QItemSelection happens to cover, keeps a shift-drag across cells from producing a
//   third, rectangular state nothing has a rule for, and leaves the view's selection MODE alone -- which matters,
//   because QAbstractItemView::rowsAboutToBeRemoved gates its "keep one item selected" block on that mode (lesson
//   Q41, found in Phase 15e when the TREE's mode changed).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QHeaderView>
#include <QRect>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// EDITOR-16's header selection: one row, or one column, or neither. Never both, and never several -- see the
	// requirement for why a disjoint set is deliberately absent.
	//-----------------------------------------------------------------------------------------------------------------

	enum class HeaderSelectionKind
	{
		None,
		Row,
		Column
	};

	struct HeaderSelection
	{
		HeaderSelectionKind kind  = HeaderSelectionKind::None;
		int                 index = -1;

		bool is_active () const
		{
			return ( kind != HeaderSelectionKind::None ) && ( index >= 0 );
		}

		// Does this selection cover the given cell? Asked per cell while painting, so it is deliberately trivial.

		bool covers ( int row, int column ) const
		{
			if ( kind == HeaderSelectionKind::Row    ) return row    == index;
			if ( kind == HeaderSelectionKind::Column ) return column == index;

			return false;
		}

		// Does it cover a whole SECTION of a header of the given orientation? The horizontal header's sections are
		// columns and the vertical header's are rows, which is the one place the two orientations differ in what
		// they are being asked.

		bool covers_section ( Qt::Orientation orientation, int section ) const
		{
			const HeaderSelectionKind sectionKind = ( orientation == Qt::Horizontal )
			                                      ? HeaderSelectionKind::Column
			                                      : HeaderSelectionKind::Row;

			return ( kind == sectionKind ) && ( index == section );
		}
	};

	//*****************************************************************************************************************
	// Class: GridHeaderView
	//*****************************************************************************************************************

	class GridHeaderView : public QHeaderView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit GridHeaderView ( Qt::Orientation orientation, QWidget* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The SORT ZONE inside a section's rectangle: full height, fixed width, at the right. The one definition the
		// paint and the hit test share. Empty for a vertical header, which carries no zone -- a row index is not a
		// column to sort by -- and empty for a section too narrow to leave the label any room.

		QRect sort_zone_rect ( const QRect& sectionRect ) const;

		// The triangle's rectangle, centred inside the zone. Purely a mark: nothing hit-tests against it.

		QRect sort_marker_rect ( const QRect& sectionRect ) const;

		// Does a viewport position land in a section's sort zone? False everywhere on a vertical header, and false on
		// a horizontal one outside the zone -- which is the "a click outside it selects" half of EDITOR-03.

		bool hits_sort_zone ( const QPoint& viewportPosition ) const;

		int           sort_marker_section () const;               // -1 when no column is marked.
		Qt::SortOrder sort_marker_order   () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// The shared EDITOR-16 selection, owned by FormGridController and read (never written) here. Null is legal
		// and means "nothing is ever selected", which is what the object form's hidden headers want.

		void set_selection_source ( const HeaderSelection* source );

		// EDIT-15's marker. set_sort_marker names the column the array is currently ordered by; clear_sort_marker is
		// called by everything that could make that claim false -- a re-present, and any change to the array.

		void set_sort_marker   ( int section, Qt::SortOrder order );
		void clear_sort_marker ();

		// Repaint after the shared selection changed underneath us. The header does not own that state and so cannot
		// notice the change itself; whoever writes it says so here.

		void refresh ();

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A click on a section, away from its sort marker: EDITOR-16 selects that row or column.

		void section_selected ( int section );

		// A press on a section's sort marker: EDIT-15 sorts by that column, ascending first.

		void sort_toggled ( int section );

		// EDITOR-19 / EDITOR-22: a context-menu gesture on a COLUMN header's name zone or on a ROW index -- the same
		// region a click selects from, which is what makes the menu's commands unambiguously about the column or the
		// row the pointer is over. The position is global because that is what QMenu::exec wants and the header is
		// the only thing that knows where the gesture landed.

		void section_menu_requested ( int section, const QPoint& globalPosition );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void  paintSection         ( QPainter* painter, const QRect& rect, int logicalIndex ) const override;

		// THE SORT ZONE IS PART OF WHAT A SECTION HAS TO FIT. QHeaderView measures a section from its text and its
		// margins, which knows nothing about a band this class paints inside the same rectangle -- so an auto-fit
		// (a double click on the divider, or any ResizeToContents pass) sized the section to its name alone and the
		// zone was then drawn straight over the last characters of it.
		//
		// Overridden here rather than compensated at each caller because everything that measures a section goes
		// through this one function, sectionSizeHint included: FormView::size_columns used to add the allowance
		// itself, which left the double click -- the one path it could not reach -- as the odd one out. Same argument
		// sort_zone_rect makes about the paint and the hit test.

		QSize sectionSizeFromContents ( int logicalIndex ) const override;

		// EDITOR-17's row numbers stand a SPACE clear of the column's right edge (2026-09-25). They are right aligned --
		// the model states that, as it states the column names' left alignment -- and right aligned alone they would
		// sit on the grid line with only the style's hairline margin between. The space is added to the painted label
		// here, and to the measured size above, so it is part of what a section has to fit rather than something a
		// four-digit index can be clipped by.

		void initStyleOptionForIndex ( QStyleOptionHeader* option, int logicalIndex ) const override;
		void  mousePressEvent      ( QMouseEvent* event ) override;
		void  contextMenuEvent     ( QContextMenuEvent* event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void paint_selection_overlay ( QPainter* painter, const QRect& rect, int logicalIndex ) const;
		void paint_sort_zone         ( QPainter* painter, const QRect& rect, int logicalIndex ) const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		const HeaderSelection* selectionSource = nullptr;         // Non-owning; the controller's.

		int           markerSection = -1;
		Qt::SortOrder markerOrder   = Qt::AscendingOrder;
	};
}

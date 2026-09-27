//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GridHeaderView implementation. See GridHeaderView.hpp for why the marker is painted here rather than by
//   QHeaderView, why the press is intercepted and the click is not, and why the selection is not the view's own.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/GridHeaderView.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionHeader>

namespace vje
{
	namespace
	{
		// The row number's right-hand gap: one ordinary space, in the header's own font. A right-aligned layout KEEPS a
		// trailing space -- measured: the rendered gap with it is the style's margin plus a space's advance, and
		// without it the margin alone, which is the pair row_numbers_are_right_aligned_a_space_clear_of_the_edge holds.

		constexpr QChar ROW_NUMBER_GAP = QLatin1Char ( ' ' );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	GridHeaderView::GridHeaderView ( Qt::Orientation orientation, QWidget* parent )
		: QHeaderView ( orientation, parent )
	{
		// Clickable, because EDITOR-16's selection is a click on a section. Qt's own sort indicator stays OFF: its
		// hit target is the whole section, which is the one thing this header cannot give it (see the header file).

		setSectionsClickable  ( true );
		setSortIndicatorShown ( false );

		// The press that reaches the base class is the one that did NOT hit a sort marker, so every sectionClicked
		// that arrives here is a selection. A resize drag emits none, so dragging a divider selects nothing.

		connect ( this, &QHeaderView::sectionClicked, this, &GridHeaderView::section_selected );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QRect GridHeaderView::sort_zone_rect ( const QRect& sectionRect ) const
	{
		// A row index is not a column to sort by, so a vertical header carries no zone -- and therefore no hit region
		// either, which is what makes every click on a row index a selection.

		if ( orientation () != Qt::Horizontal )
		{
			return QRect ();
		}

		// A section too narrow to leave the label any room carries no zone. Painting one there would cover the
		// column's name, and -- because this rect IS the hit region -- would leave a narrow column impossible to
		// SELECT, its whole width having become the sort control.

		const int requiredWidth = config::form::SORT_ZONE_WIDTH + config::form::SORT_ZONE_MINIMUM_LABEL_WIDTH;

		if ( sectionRect.width () < requiredWidth )
		{
			return QRect ();
		}

		// Full height, so the target is the whole right-hand band of the header rather than a spot inside it: a user
		// aiming at the sort control should not have to aim vertically at all.

		return QRect
		(
			sectionRect.right () - config::form::SORT_ZONE_WIDTH + 1,
			sectionRect.top (),
			config::form::SORT_ZONE_WIDTH,
			sectionRect.height ()
		);
	}

	QRect GridHeaderView::sort_marker_rect ( const QRect& sectionRect ) const
	{
		const QRect zone = sort_zone_rect ( sectionRect );

		if ( zone.isEmpty () )
		{
			return QRect ();
		}

		// Centred in the zone. It is a MARK rather than a target -- nothing hit-tests against it -- so its size is
		// free to be whatever reads clearly.

		return QRect
		(
			zone.center ().x () - ( config::form::SORT_MARKER_WIDTH / 2 ),
			zone.center ().y () - ( config::form::SORT_MARKER_HEIGHT / 2 ),
			config::form::SORT_MARKER_WIDTH,
			config::form::SORT_MARKER_HEIGHT
		);
	}

	bool GridHeaderView::hits_sort_zone ( const QPoint& viewportPosition ) const
	{
		if ( orientation () != Qt::Horizontal )
		{
			return false;
		}

		const int section = logicalIndexAt ( viewportPosition );

		if ( section < 0 )
		{
			return false;
		}

		const QRect sectionRect
		(
			sectionViewportPosition ( section ),
			0,
			sectionSize ( section ),
			height ()
		);

		const QRect zone = sort_zone_rect ( sectionRect );

		return !zone.isEmpty () && zone.contains ( viewportPosition );
	}

	int GridHeaderView::sort_marker_section () const
	{
		return markerSection;
	}

	Qt::SortOrder GridHeaderView::sort_marker_order () const
	{
		return markerOrder;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void GridHeaderView::set_selection_source ( const HeaderSelection* source )
	{
		selectionSource = source;

		refresh ();
	}

	void GridHeaderView::set_sort_marker ( int section, Qt::SortOrder order )
	{
		markerSection = section;
		markerOrder   = order;

		refresh ();
	}

	void GridHeaderView::clear_sort_marker ()
	{
		markerSection = -1;
		markerOrder   = Qt::AscendingOrder;

		refresh ();
	}

	void GridHeaderView::refresh ()
	{
		viewport ()->update ();
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void GridHeaderView::paintSection ( QPainter* painter, const QRect& rect, int logicalIndex ) const
	{
		QHeaderView::paintSection ( painter, rect, logicalIndex );

		paint_selection_overlay ( painter, rect, logicalIndex );
		paint_sort_zone         ( painter, rect, logicalIndex );
	}

	QSize GridHeaderView::sectionSizeFromContents ( int logicalIndex ) const
	{
		QSize size = QHeaderView::sectionSizeFromContents ( logicalIndex );

		// Only the horizontal header carries a zone, and only a section wide enough to leave the label room gets one
		// (sort_zone_rect) -- but the allowance is added UNCONDITIONALLY here, and that is deliberate rather than
		// sloppy. Asking sort_zone_rect first would need the width being computed, which is what this function is
		// returning: a section is narrow, so it gets no zone, so it stays narrow. Adding the band is what makes it
		// wide enough to have one.

		if ( orientation () == Qt::Horizontal )
		{
			size.setWidth ( size.width () + config::form::SORT_ZONE_WIDTH );
		}
		else
		{
			size.setWidth ( size.width () + fontMetrics ().horizontalAdvance ( QString ( ROW_NUMBER_GAP ) ) );
		}

		return size;
	}

	void GridHeaderView::initStyleOptionForIndex ( QStyleOptionHeader* option, int logicalIndex ) const
	{
		QHeaderView::initStyleOptionForIndex ( option, logicalIndex );

		// The gap is TEXT, which is what makes it a space in the header's own font at every display scaling -- a pixel
		// inset would be a second measurement of the same thing. It is appended to the painted label only, never to
		// the model's header data, so nothing that reads the row number as a value sees it.

		if ( ( orientation () == Qt::Vertical ) && !option->text.isEmpty () )
		{
			option->text += QString ( ROW_NUMBER_GAP );
		}
	}

	void GridHeaderView::mousePressEvent ( QMouseEvent* event )
	{
		const QPoint position = event->position ().toPoint ();

		// A press in the sort ZONE is CONSUMED, so QHeaderView never sees it and never emits sectionClicked -- which
		// is what keeps a sort from also selecting the column it sorted. Everything else, resize drags included, is
		// the base class's unchanged.

		if ( ( event->button () == Qt::LeftButton ) && hits_sort_zone ( position ) )
		{
			const int section = logicalIndexAt ( position );

			if ( section >= 0 )
			{
				emit sort_toggled ( section );

				event->accept ();

				return;
			}
		}

		QHeaderView::mousePressEvent ( event );
	}

	void GridHeaderView::contextMenuEvent ( QContextMenuEvent* event )
	{
		// EDITOR-19 for a column header and EDITOR-22 for a row index, and the one exclusion is the whole of the rule.
		//
		// The SORT ZONE raises nothing. It is a control in its own right (EDITOR-03's resolution of the
		// select-versus-sort collision), and a control's own area is not somewhere to right-click a menu about the
		// thing underneath it -- so the menu belongs to the NAME zone, which is exactly the region a left click
		// selects from. One rect, hits_sort_zone, answers both questions, so the boundary the user learned by
		// clicking is the same boundary the menu obeys.
		//
		// A row index has no sort zone (sort_zone_rect answers an empty rect for a vertical header), so the whole of
		// every section raises the request there -- which is again exactly the region a left click selects from.
		// Which menu the request opens is the controller's business, not the header's: the header says only where
		// the gesture landed.
		//
		// The vertical header raised nothing until 2026-09-23, on the reasoning that a row already had Delete on the
		// keyboard and no Clear Contents worth offering. The first half was untrue -- the key never reached a
		// selected row (FormGridController::handle_delete_key says why) -- and the second was answered by the user
		// asking for exactly that command.

		const QPoint position = event->pos ();
		const int    section  = logicalIndexAt ( position );

		if ( ( section < 0 ) || hits_sort_zone ( position ) )
		{
			QHeaderView::contextMenuEvent ( event );

			return;
		}

		// mapToGlobal rather than the event's own globalPos, so the two coordinate systems in play here are the
		// widget's and the screen's and nothing has to be remembered about which the event reports in.

		emit section_menu_requested ( section, mapToGlobal ( position ) );

		event->accept ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void GridHeaderView::paint_selection_overlay ( QPainter* painter, const QRect& rect, int logicalIndex ) const
	{
		if ( ( selectionSource == nullptr ) || !selectionSource->covers_section ( orientation (), logicalIndex ) )
		{
			return;
		}

		QColor tint = palette ().color ( QPalette::Highlight );
		tint.setAlpha ( config::form::SELECTED_SECTION_TINT_ALPHA );

		painter->save ();
		painter->fillRect ( rect, tint );
		painter->restore ();
	}

	void GridHeaderView::paint_sort_zone ( QPainter* painter, const QRect& rect, int logicalIndex ) const
	{
		const QRect zone = sort_zone_rect ( rect );

		if ( zone.isEmpty () )
		{
			return;
		}

		// EVERY column carries a zone, because a target that cannot be seen is not one. The column the array is
		// actually ordered by is marked at full strength and the rest are toned back, so the affordance is
		// discoverable without several full-strength triangles reading as several controls asking to be pressed.
		//
		// An idle marker points UP because that is what the next click on it does: EDIT-15 sorts ascending first.

		const bool          isMarked = ( logicalIndex == markerSection );
		const Qt::SortOrder order    = isMarked ? markerOrder : Qt::AscendingOrder;

		const QColor textColour = palette ().color ( QPalette::ButtonText );
		const QColor colour     = isMarked
		                        ? textColour
		                        : contrasting_tone ( textColour, config::form::SORT_MARKER_IDLE_CONTRAST );

		painter->save ();

		// The DIVIDER is what makes the two zones legible as two. Toned from the header's own surface rather than
		// drawn in the text colour: it separates two regions of one control, and a full-strength rule would read as a
		// column boundary -- which is the one thing it is not.

		const QColor divider = contrasting_tone
		(
			palette ().color ( QPalette::Button ),
			config::form::SORT_ZONE_DIVIDER_CONTRAST
		);

		painter->setPen ( divider );
		painter->drawLine ( zone.topLeft (), zone.bottomLeft () );

		// Ascending points up, descending points down -- the literal reading, and the one that survives being
		// described in a sentence. Qt's own indicator is not used at all here, so its style-dependent convention
		// (which inverts between styles) never enters into it.

		const QRect marker = sort_marker_rect ( rect );

		QPainterPath triangle;

		if ( order == Qt::AscendingOrder )
		{
			triangle.moveTo ( marker.center ().x () + 0.5, marker.top () );
			triangle.lineTo ( marker.right () + 1.0,       marker.bottom () + 1.0 );
			triangle.lineTo ( marker.left (),              marker.bottom () + 1.0 );
		}
		else
		{
			triangle.moveTo ( marker.center ().x () + 0.5, marker.bottom () + 1.0 );
			triangle.lineTo ( marker.right () + 1.0,       marker.top () );
			triangle.lineTo ( marker.left (),              marker.top () );
		}

		triangle.closeSubpath ();

		painter->setRenderHint ( QPainter::Antialiasing, true );
		painter->setPen   ( Qt::NoPen );
		painter->setBrush ( colour );
		painter->drawPath ( triangle );
		painter->restore ();
	}
}

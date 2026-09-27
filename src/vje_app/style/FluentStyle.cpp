//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FluentStyle implementation -- see FluentStyle.hpp.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FluentStyle.hpp"

#include "AppConfig.hpp"

#include <QDialogButtonBox>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionComplex>
#include <QStyleOptionMenuItem>
#include <QStyleOptionTab>

namespace vje
{
	namespace
{
		//-------------------------------------------------------------------------------------------------------------
		// WHICH ELEMENTS ARE ROUNDED, AND WHY THE LIST STOPS WHERE IT DOES.
		//
		// STYLE-05 says "controls use the ~4 px radius". A control here means something the user types into or presses:
		// push buttons, tool buttons, line edits, combo boxes and spin boxes. That is the whole list, and the omissions
		// are deliberate rather than pending:
		//
		//   - CHECK BOXES and RADIO BUTTONS are indicators a few pixels across, drawn as a box or a circle. A 4 px
		//     radius on a 13 px box is not a fillet, it is a different shape.
		//   - SCROLL BARS, SLIDERS and PROGRESS BARS are not surfaces the radius discipline is about; rounding a scroll
		//     bar's groove pulls its ends away from the pane edge they are meant to meet (the same problem that squared
		//     the cards' BOTTOM corners in Phase 14, seen from the other side).
		//   - TABS are painted by views/ShadedTabBar from its own rule (architecture 9.4a) and must not be reached from
		//     two places.
		//   - FRAMES and GROUP BOXES are containers, which STYLE-05 gives the ~8 px radius and which paint themselves --
		//     views/Card for the workspace panes, dialogs/GroupBox for the Settings dialog's boxes (SET-01c). A frame
		//     rounded HERE would be another statement of a container's shape, and a group box's outline needs a
		//     colour, which this class never states.
		//-------------------------------------------------------------------------------------------------------------

		bool is_rounded_primitive ( QStyle::PrimitiveElement element )
		{
			return ( element == QStyle::PE_PanelButtonCommand )
			    || ( element == QStyle::PE_PanelButtonBevel )
			    || ( element == QStyle::PE_PanelButtonTool )
			    || ( element == QStyle::PE_PanelLineEdit )
			    || ( element == QStyle::PE_FrameLineEdit );
		}

		bool is_rounded_complex_control ( QStyle::ComplexControl control )
		{
			return ( control == QStyle::CC_ComboBox ) || ( control == QStyle::CC_SpinBox );
		}

		// A tool button that is neither hovered, pressed nor checked draws NOTHING at all in Fusion, so rounding it
		// would allocate an image, erase the corners of a transparent one and stroke it in a transparent colour. The
		// whole toolbar is in that state most of the time (NFR-03), so it is worth one cheap question up front.

		bool draws_nothing ( QStyle::PrimitiveElement element, const QStyleOption* option )
		{
			if ( ( element != QStyle::PE_PanelButtonTool ) || ( option == nullptr ) )
			{
				return false;
			}

			return !option->state.testAnyFlags ( QStyle::State_MouseOver | QStyle::State_On | QStyle::State_Sunken );
		}

		//-------------------------------------------------------------------------------------------------------------
		// The four corner wedges: the region between the element's square bounds and its rounded outline. Erasing this
		// is what rounds the corners, and it is the same construction views/Card uses -- stated separately there
		// because a card composes its wedges OVER its content while this one erases them OUT of a rendered element.
		//-------------------------------------------------------------------------------------------------------------

		QPainterPath corner_wedges ( const QRectF& bounds, qreal radius )
		{
			QPainterPath rounded;
			rounded.addRoundedRect ( bounds, radius, radius );

			QPainterPath square;
			square.addRect ( bounds );

			return square.subtracted ( rounded );
		}

		//-------------------------------------------------------------------------------------------------------------
		// The border colour the base style just used, taken from the pixels it produced rather than computed.
		//
		// This is what keeps the class free of colour. Sampled at the left edge, vertically centred: every control in
		// the rounded set draws its outline as a flat colour along its straight edges, so one pixel answers -- and it
		// answers for the state as well as the theme, since a focused line edit's highlight border and a disabled
		// button's muted one are simply what is there to sample.
		//
		// A fully transparent sample means the base style drew no border, and the re-stroke then paints nothing, which
		// is the correct outcome rather than a fallback.
		//-------------------------------------------------------------------------------------------------------------

		QColor sampled_border_colour ( const QImage& rendered )
		{
			if ( rendered.isNull () || ( rendered.width () < 1 ) || ( rendered.height () < 1 ) )
			{
				return QColor ( Qt::transparent );
			}

			return rendered.pixelColor ( 0, rendered.height () / 2 );
		}

		//-------------------------------------------------------------------------------------------------------------
		// Render `draw` into a transparent image the size of `rect`, round its corners, close its outline, and blit it.
		//
		// The image is device-pixel-ratio aware, so the mask and the re-stroke are antialiased at the device resolution
		// rather than at the logical one -- which on a high-DPI display is the difference between a smooth arc and a
		// stepped one.
		//-------------------------------------------------------------------------------------------------------------

		template <typename DrawFunction>
		void draw_rounded ( const QRect& rect, QPainter* painter, DrawFunction draw )
		{
			const qreal ratio = ( painter->device () != nullptr ) ? painter->device ()->devicePixelRatioF () : 1.0;

			QImage rendered ( rect.size () * ratio, QImage::Format_ARGB32_Premultiplied );

			rendered.setDevicePixelRatio ( ratio );
			rendered.fill ( Qt::transparent );

			{
				QPainter renderer ( &rendered );

				// The element is asked for at its OWN coordinates and lands at the image's origin, so the option --
				// including its rect -- is passed through untouched. Translating the painter rather than copying and
				// adjusting the option matters: a QStyleOption cannot be copied without slicing its subclass away.

				renderer.translate ( -rect.topLeft () );
				draw ( &renderer );
			}

			const QColor  border = sampled_border_colour ( rendered );
			const QRectF  bounds ( 0.0, 0.0, rect.width (), rect.height () );
			const qreal   radius = config::appearance::CONTROL_CORNER_RADIUS;

			{
				QPainter shaper ( &rendered );

				shaper.setRenderHint ( QPainter::Antialiasing, true );

				// Erase the corners.

				shaper.setCompositionMode ( QPainter::CompositionMode_DestinationOut );
				shaper.fillPath ( corner_wedges ( bounds, radius ), Qt::black );

				// Close the outline the erase just broke. Inset by half a pixel so a one-pixel pen lands ON the
				// element's edge rather than straddling it, which is the same alignment rule the icon set is drawn to.

				shaper.setCompositionMode ( QPainter::CompositionMode_SourceOver );
				shaper.setPen ( QPen ( border, 1.0 ) );
				shaper.setBrush ( Qt::NoBrush );
				shaper.drawRoundedRect ( bounds.adjusted ( 0.5, 0.5, -0.5, -0.5 ), radius, radius );
			}

			painter->drawImage ( rect.topLeft (), rendered );
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FluentStyle::FluentStyle ( QStyle* baseStyle, config::appearance::InterfaceStyle style )
		: QProxyStyle ( baseStyle )
		, style       ( style )
	{
	}

	//=================================================================================================================
	// QStyle Overrides
	//=================================================================================================================

	QSize FluentStyle::sizeFromContents
	(
		ContentsType        type,
		const QStyleOption* option,
		const QSize&        contentsSize,
		const QWidget*      widget
	) const
	{
		QSize size = QProxyStyle::sizeFromContents ( type, option, contentsSize, widget );

		if ( type == CT_MenuItem )
		{
			const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*> ( option );

			if ( menuItem != nullptr )
			{
				if ( menuItem->menuItemType == QStyleOptionMenuItem::Separator )
				{
					// ABSOLUTE, not a floor. A separator is a rule plus padding -- it has no text or icon to clip --
					// so it is safe to set outright, and it MUST be, or the value could not be tuned below the base
					// style's own separator height (13 on Fusion / Qt 6.10.1). A qMax here silently ignores any
					// smaller setting.

					size.setHeight ( config::menu::SEPARATOR_HEIGHT );
				}
				else
				{
					// A FLOOR, never a truncation: a row carries text and icons, so a large font or a tall icon must
					// still fit. The base style's computed height wins whenever it is already the larger of the two.

					size.setHeight ( qMax ( size.height (), config::menu::ITEM_MINIMUM_HEIGHT ) );
				}
			}
		}

		return size;
	}

	int FluentStyle::pixelMetric ( PixelMetric metric, const QStyleOption* option, const QWidget* widget ) const
	{
		switch ( metric )
		{
			case PM_MenuVMargin: return config::menu::VERTICAL_MARGIN;
			case PM_MenuHMargin: return config::menu::HORIZONTAL_MARGIN;

			// The toolbar's group breaks (File | Undo/Redo | Add) carry the same separation
			// role the menu separator does, so they get the same treatment.

			case PM_ToolBarSeparatorExtent: return config::toolbar::SEPARATOR_EXTENT;

			// The message-box icon (STYLE-18 (5)). Fusion's is 48 (measured), half as large again as a Windows message
			// box's 32; QMessageBox asks for this metric when it sets its icon, so one answer sizes every box. Under both
			// interface styles, like the menu metrics: it is a size, not a shape.

			case PM_MessageBoxIconSize: return config::message_box::ICON_SIZE;

			default: break;
		}

		return QProxyStyle::pixelMetric ( metric, option, widget );
	}

	int FluentStyle::styleHint ( StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData ) const
	{
		switch ( hint )
		{
			// WINDOWS' BUTTON ORDER ON EVERY PLATFORM (LD-9). QDialogButtonBox asks the style where each role goes, and
			// Fusion forwards the question to the platform theme -- so without this answer the order would follow
			// whichever Linux desktop VJE runs on. Measured (Qt 6.10.1, 2026-09-26): a base style answering GnomeLayout
			// laid Save / Don't Save / Cancel out as Discard, Cancel, Save. One answer here puts the message boxes, the
			// framed dialogs (STYLE-15) and the text prompts into the same order, with no call site involved.

			case SH_DialogButtonLayout: return QDialogButtonBox::WinLayout;

			// Right-aligned rather than centred, which is the Windows placement STYLE-18 (3) names.

			case SH_MessageBox_CenterButtons: return 0;

			default: break;
		}

		return QProxyStyle::styleHint ( hint, option, widget, returnData );
	}

	QRect FluentStyle::subElementRect ( SubElement element, const QStyleOption* option, const QWidget* widget ) const
	{
		const QRect base = QProxyStyle::subElementRect ( element, option, widget );

		if ( element != SE_TabBarTabRightButton )
		{
			return base;
		}

		const QStyleOptionTab* const tab = qstyleoption_cast<const QStyleOptionTab*> ( option );

		if ( ( tab == nullptr ) || base.isEmpty () || tab->rect.isEmpty () )
		{
			return base;
		}

		// THE CLOSE BUTTON SITS AT AN EQUAL DISTANCE FROM THE TAB'S TOP, BOTTOM AND RIGHT EDGES (STYLE-11).
		//
		// The margin is DERIVED rather than tuned: it is whatever centring the button vertically already leaves, then
		// applied on the right as well. So there is no number here and none in AppConfig -- the rule is an equality,
		// and a constant would be a second thing to keep in step with the button's size and the tab's height, either
		// of which the base style may move between Qt releases (the close indicator is 20 and the tab 32 today, so the
		// margin is 6; Fusion's own right inset was 13, derived from PM_TabBarTabHSpace and equal to nothing).
		//
		// FUSION'S UNSELECTED-TAB SHIFT IS DELIBERATELY NOT CARRIED. The base style nudges an unselected tab's contents
		// down (PM_TabBarTabShiftVertical), which put the button at 7 above and 5 below -- unequal, and unequal in a
		// way our own strip contradicts: ShadedTabBar paints every tab as the same block from the same rect, with no
		// shift in the surface for the contents to follow. Pinning the button to the tab's rect is what makes the three
		// gaps equal on every tab rather than only on the selected one.
		//
		// The vertical centring is exact only when the leftover is even, which it is at every size the base style
		// currently produces; an odd leftover would put the extra pixel below, and one pixel is not worth a rule.

		const int margin = ( tab->rect.height () - base.height () ) / 2;

		QRect placed = base;

		placed.moveTop   ( tab->rect.top ()   + margin );
		placed.moveRight ( tab->rect.right () - margin );

		return placed;
	}

	void FluentStyle::drawPrimitive
	(
		PrimitiveElement    element,
		const QStyleOption* option,
		QPainter*           painter,
		const QWidget*      widget
	) const
	{
		const bool round = config::appearance::rounds_controls ( style )
		                && is_rounded_primitive ( element )
		                && ( option != nullptr )
		                && !option->rect.isEmpty ()
		                && !draws_nothing ( element, option );

		if ( !round )
		{
			QProxyStyle::drawPrimitive ( element, option, painter, widget );
			return;
		}

		draw_rounded
		(
			option->rect,
			painter,
			[ this, element, option, widget ] ( QPainter* target )
			{
				QProxyStyle::drawPrimitive ( element, option, target, widget );
			}
		);
	}

	void FluentStyle::drawComplexControl
	(
		ComplexControl             control,
		const QStyleOptionComplex* option,
		QPainter*                  painter,
		const QWidget*             widget
	) const
	{
		const bool round = config::appearance::rounds_controls ( style )
		                && is_rounded_complex_control ( control )
		                && ( option != nullptr )
		                && !option->rect.isEmpty ();

		if ( !round )
		{
			QProxyStyle::drawComplexControl ( control, option, painter, widget );
			return;
		}

		draw_rounded
		(
			option->rect,
			painter,
			[ this, control, option, widget ] ( QPainter* target )
			{
				QProxyStyle::drawComplexControl ( control, option, target, widget );
			}
		);
	}

	//=================================================================================================================
	// Accessors
	//=================================================================================================================

	config::appearance::InterfaceStyle FluentStyle::interface_style () const
	{
		return style;
	}
}

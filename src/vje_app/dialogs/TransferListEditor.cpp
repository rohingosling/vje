//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TransferListEditor implementation. See the header for the move-versus-copy asymmetry and why order is restored
//   rather than appended.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/TransferListEditor.hpp"

#include "AppConfig.hpp"

#include <QAbstractItemModel>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// A push button whose arrow is PAINTED rather than typed.
		//
		// WHY NOT A TEXT GLYPH, which is what this control shipped with. The four arrows were the characters U+25C0 /
		// U+25B6 / U+25B2 / U+25BC, and on Windows they do not come from one font: Segoe UI carries the up and down
		// pair but not the left and right one, so those two fell through to MS UI Gothic and rendered at 5 x 12 against
		// the others' 9 x 16 -- half the size, different outlines, visibly not a set. On top of that, ClearType applies
		// SUBPIXEL antialiasing to text, which on a solid triangle's hypotenuse paints orange and blue fringes that
		// read as a ragged edge rather than as smoothing. Both were measured, not guessed.
		//
		// Painting removes the font from the question entirely: one triangle rotated four ways, so the set cannot drift
		// apart, greyscale antialiasing rather than subpixel, and the same palette colour the text was taking anyway.
		// The widget still needs no IconLibrary, which is what kept it a general settings control (see the header).
		//
		// No Q_OBJECT: it adds no signal or slot of its own, so it needs no moc and stays local to this file.
		//-------------------------------------------------------------------------------------------------------------

		class ArrowButton : public QPushButton
		{
		public:

			enum class Direction { Left, Right, Up, Down };

			ArrowButton ( Direction direction, QWidget* parent )
			:	QPushButton ( parent ),
				direction   ( direction )
			{
			}

		protected:

			void paintEvent ( QPaintEvent* event ) override
			{
				QPushButton::paintEvent ( event );   // The button chrome, from the style. Only the label is ours.

				QPainter painter ( this );

				painter.setRenderHint ( QPainter::Antialiasing, true );
				painter.setPen ( Qt::NoPen );

				// Disabled takes the palette's own disabled text colour rather than a washed-out overlay, matching what
				// IconLibrary does for the icon set (its build_icon supplies both tints for the same reason).

				painter.setBrush
				(
					palette ().color ( isEnabled () ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText )
				);

				// A pressed button's label shifts by the style's own amount; the arrow travels with the chrome or it
				// looks pinned to the window while the button moves under it.

				const int horizontalShift = isDown () ? style ()->pixelMetric ( QStyle::PM_ButtonShiftHorizontal, nullptr, this ) : 0;
				const int verticalShift   = isDown () ? style ()->pixelMetric ( QStyle::PM_ButtonShiftVertical,   nullptr, this ) : 0;

				const qreal centreX = ( width ()  / 2.0 ) + horizontalShift;
				const qreal centreY = ( height () / 2.0 ) + verticalShift;

				painter.drawPolygon ( arrow ( centreX, centreY ) );
			}

		private:

			// One triangle, base 2h and height h, rotated. Equal area in all four directions is what makes "the four
			// arrows are the same size" a property of the geometry rather than of four separate glyph designs.

			QPolygonF arrow ( qreal centreX, qreal centreY ) const
			{
				const qreal extent = config::settings_dialog::transfer_list::ARROW_HALF_EXTENT;

				QPolygonF triangle;

				switch ( direction )
				{
					case Direction::Left:
					{
						triangle << QPointF ( centreX - extent, centreY )
						         << QPointF ( centreX + extent, centreY - extent )
						         << QPointF ( centreX + extent, centreY + extent );
						break;
					}

					case Direction::Right:
					{
						triangle << QPointF ( centreX + extent, centreY )
						         << QPointF ( centreX - extent, centreY - extent )
						         << QPointF ( centreX - extent, centreY + extent );
						break;
					}

					case Direction::Up:
					{
						triangle << QPointF ( centreX, centreY - extent )
						         << QPointF ( centreX - extent, centreY + extent )
						         << QPointF ( centreX + extent, centreY + extent );
						break;
					}

					default:
					{
						triangle << QPointF ( centreX, centreY + extent )
						         << QPointF ( centreX - extent, centreY - extent )
						         << QPointF ( centreX + extent, centreY - extent );
						break;
					}
				}

				return triangle;
			}

			Direction direction;
		};

		// One arrow button, sized and named. Free rather than a member because its direction type is local to this file
		// and nothing in the class's interface needs to name it.

		QPushButton* build_arrow_button
		(
			ArrowButton::Direction direction,
			const QString&         name,
			const QString&         tooltip,
			QWidget*               parent
		)
		{
			namespace metrics = config::settings_dialog::transfer_list;

			QPushButton* const button = new ArrowButton ( direction, parent );

			button->setFixedSize ( metrics::BUTTON_SIZE, metrics::BUTTON_SIZE );
			button->setToolTip ( tooltip );

			// The button carries NO text: the arrow is painted, and the name is what a screen reader announces. That
			// separation is the point -- the label is no longer a font's opinion of a triangle.

			button->setAccessibleName ( name );

			// See restoreButton in the constructor: Return is the lists' key here, not a push button's.

			button->setAutoDefault ( false );

			return button;
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TransferListEditor::TransferListEditor ( const SettingsField& field, const QStringList& chosenValues, QWidget* parent )
		: QWidget          ( parent )
		, options          ( field.options )
		, repeatableValue  ( field.repeatableValue )
		, defaultChosen    ( field.defaultValue.toStringList () )
	{
		for ( int index = 0; index < options.size (); ++index )
		{
			optionIndexByValue.insert ( options [ index ].value, index );
		}

		build_layout ( field );
		populate ( chosenValues );

		// Reporting rides the chosen list's own MODEL signals, so an internal drag-reorder arrives by the same route as
		// a button press and neither path can be forgotten. Connected after the initial populate, which is a fill and
		// not a change.

		connect ( chosenList->model (), &QAbstractItemModel::rowsInserted, this, &TransferListEditor::handle_chosen_changed );
		connect ( chosenList->model (), &QAbstractItemModel::rowsRemoved,  this, &TransferListEditor::handle_chosen_changed );
		connect ( chosenList->model (), &QAbstractItemModel::rowsMoved,    this, &TransferListEditor::handle_chosen_changed );

		update_button_enablement ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QStringList TransferListEditor::chosen () const
	{
		QStringList values;

		for ( int row = 0; row < chosenList->count (); ++row )
		{
			values.append ( chosenList->item ( row )->data ( Qt::UserRole ).toString () );
		}

		return values;
	}

	//=================================================================================================================
	// Construction Helpers
	//=================================================================================================================

	void TransferListEditor::build_layout ( const SettingsField& field )
	{
		namespace metrics = config::settings_dialog::transfer_list;

		chosenList    = new QListWidget ( this );
		availableList = new QListWidget ( this );

		// Object names so the suite can reach the parts without this class exposing them, and so a future QSS rule has
		// something to select on.

		chosenList->setObjectName ( object_names::CHOSEN_LIST );
		availableList->setObjectName ( object_names::AVAILABLE_LIST );

		for ( QListWidget* const list : { chosenList, availableList } )
		{
			list->setIconSize ( QSize ( metrics::ROW_ICON_SIZE, metrics::ROW_ICON_SIZE ) );
			list->setMinimumHeight ( metrics::LIST_MINIMUM_HEIGHT );
			list->setSelectionMode ( QAbstractItemView::SingleSelection );
			list->setUniformItemSizes ( true );
			list->installEventFilter ( this );

			connect ( list, &QListWidget::currentRowChanged, this, &TransferListEditor::update_button_enablement );
		}

		// Dragging within the chosen list reorders it -- the same result as the arrow buttons, by the gesture a user is
		// likeliest to try first. Dragging BETWEEN the lists is deliberately not offered (spec section 6).

		chosenList->setDragDropMode ( QAbstractItemView::InternalMove );
		chosenList->setDefaultDropAction ( Qt::MoveAction );

		connect ( chosenList,    &QListWidget::itemDoubleClicked, this, &TransferListEditor::handle_remove );
		connect ( availableList, &QListWidget::itemDoubleClicked, this, &TransferListEditor::handle_add );

		QLabel* const chosenCaption    = new QLabel ( field.chosenListLabel, this );
		QLabel* const availableCaption = new QLabel ( field.availableListLabel, this );

		chosenList->setAccessibleName ( field.chosenListLabel );
		availableList->setAccessibleName ( field.availableListLabel );

		addButton = build_arrow_button
		(
			ArrowButton::Direction::Left,                       // Into the chosen list, which sits on the left.
			tr ( "Add to %1" ).arg ( field.chosenListLabel ),
			tr ( "Add the selected command to %1 (Alt+Left)" ).arg ( field.chosenListLabel ),
			this
		);

		removeButton = build_arrow_button
		(
			ArrowButton::Direction::Right,                      // Back to the available list, on the right.
			tr ( "Remove from %1" ).arg ( field.chosenListLabel ),
			tr ( "Remove the selected command from %1 (Alt+Right)" ).arg ( field.chosenListLabel ),
			this
		);

		moveUpButton   = build_arrow_button ( ArrowButton::Direction::Up,   tr ( "Move up" ),   tr ( "Move the selected command up (Alt+Up)" ),     this );
		moveDownButton = build_arrow_button ( ArrowButton::Direction::Down, tr ( "Move down" ), tr ( "Move the selected command down (Alt+Down)" ), this );

		addButton->setObjectName ( object_names::ADD_BUTTON );
		removeButton->setObjectName ( object_names::REMOVE_BUTTON );
		moveUpButton->setObjectName ( object_names::MOVE_UP_BUTTON );
		moveDownButton->setObjectName ( object_names::MOVE_DOWN_BUTTON );

		restoreButton = new QPushButton ( tr ( "Restore Defaults" ), this );

		restoreButton->setObjectName ( object_names::RESTORE_BUTTON );

		// Never the dialog's default button: Return belongs to the list the user is in (see eventFilter), and an
		// auto-default push button here would answer it instead.

		restoreButton->setAutoDefault ( false );

		connect ( addButton,      &QPushButton::clicked, this, &TransferListEditor::handle_add );
		connect ( removeButton,   &QPushButton::clicked, this, &TransferListEditor::handle_remove );
		connect ( moveUpButton,   &QPushButton::clicked, this, &TransferListEditor::handle_move_up );
		connect ( moveDownButton, &QPushButton::clicked, this, &TransferListEditor::handle_move_down );
		connect ( restoreButton,  &QPushButton::clicked, this, &TransferListEditor::handle_restore_default );

		// The keyboard equivalents (NFR-05). Scoped to this widget and its children so Alt+Left never reaches the
		// dialog while the user is somewhere else in it.

		const auto add_shortcut = [ this ] ( const QKeySequence& keys, void ( TransferListEditor::*slot ) () )
		{
			QShortcut* const shortcut = new QShortcut ( keys, this );

			shortcut->setContext ( Qt::WidgetWithChildrenShortcut );

			connect ( shortcut, &QShortcut::activated, this, slot );
		};

		add_shortcut ( QKeySequence ( Qt::ALT | Qt::Key_Left ),  &TransferListEditor::handle_add );
		add_shortcut ( QKeySequence ( Qt::ALT | Qt::Key_Right ), &TransferListEditor::handle_remove );
		add_shortcut ( QKeySequence ( Qt::ALT | Qt::Key_Up ),    &TransferListEditor::handle_move_up );
		add_shortcut ( QKeySequence ( Qt::ALT | Qt::Key_Down ),  &TransferListEditor::handle_move_down );

		// -- Assembly. Chosen list left, transfer buttons centred between, available list right; the reorder pair sits
		//    directly beneath the list it acts on, and Restore Defaults under the other one.

		QVBoxLayout* const transferColumn = new QVBoxLayout ();

		transferColumn->setContentsMargins ( 0, 0, 0, 0 );
		transferColumn->addStretch ( 1 );
		transferColumn->addWidget ( addButton );
		transferColumn->addSpacing ( metrics::CONTROL_GAP );
		transferColumn->addWidget ( removeButton );
		transferColumn->addStretch ( 1 );

		QHBoxLayout* const reorderRow = new QHBoxLayout ();

		reorderRow->setContentsMargins ( 0, 0, 0, 0 );
		reorderRow->addWidget ( moveUpButton );
		reorderRow->addWidget ( moveDownButton );
		reorderRow->addStretch ( 1 );

		QHBoxLayout* const restoreRow = new QHBoxLayout ();

		restoreRow->setContentsMargins ( 0, 0, 0, 0 );
		restoreRow->addStretch ( 1 );
		restoreRow->addWidget ( restoreButton );

		QGridLayout* const grid = new QGridLayout ( this );

		grid->setContentsMargins ( 0, 0, 0, 0 );
		grid->setHorizontalSpacing ( metrics::COLUMN_GAP );
		grid->setVerticalSpacing ( metrics::CONTROL_GAP );

		grid->addWidget ( chosenCaption,    0, 0 );
		grid->addWidget ( availableCaption, 0, 2 );
		grid->addWidget ( chosenList,       1, 0 );
		grid->addLayout ( transferColumn,   1, 1 );
		grid->addWidget ( availableList,    1, 2 );
		grid->addLayout ( reorderRow,       2, 0 );
		grid->addLayout ( restoreRow,       2, 2 );

		grid->setColumnStretch ( 0, 1 );
		grid->setColumnMinimumWidth ( 1, metrics::BUTTON_COLUMN_WIDTH );
		grid->setColumnStretch ( 2, 1 );
		grid->setRowStretch ( 1, 1 );

		// Tab order follows the page's own reading: arrange the toolbar, then reach for what to add to it.

		setTabOrder ( chosenList,     moveUpButton );
		setTabOrder ( moveUpButton,   moveDownButton );
		setTabOrder ( moveDownButton, addButton );
		setTabOrder ( addButton,      removeButton );
		setTabOrder ( removeButton,   availableList );
		setTabOrder ( availableList,  restoreButton );
	}

	void TransferListEditor::populate ( const QStringList& chosenValues )
	{
		chosenList->clear ();
		availableList->clear ();

		QSet<QString> taken;

		for ( const QString& value : chosenValues )
		{
			const bool repeatable = !repeatableValue.isEmpty () && ( value == repeatableValue );

			// Tolerant rather than strict: an entry naming no option (a command this build has not got, or a settings
			// file from a newer one) and a repeat of a unique option are both skipped, so the page always opens.

			if ( !optionIndexByValue.contains ( value ) || ( !repeatable && taken.contains ( value ) ) )
			{
				continue;
			}

			chosenList->addItem ( build_item ( value ) );

			if ( !repeatable )
			{
				taken.insert ( value );
			}
		}

		for ( const SettingsOption& option : options )
		{
			const bool repeatable = !repeatableValue.isEmpty () && ( option.value == repeatableValue );

			if ( repeatable || !taken.contains ( option.value ) )
			{
				availableList->addItem ( build_item ( option.value ) );
			}
		}
	}

	QListWidgetItem* TransferListEditor::build_item ( const QString& value ) const
	{
		const SettingsOption& option = options [ optionIndexByValue.value ( value ) ];

		QListWidgetItem* const item = new QListWidgetItem ( option.icon, option.label );

		item->setData ( Qt::UserRole, value );

		return item;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void TransferListEditor::handle_add ()
	{
		QListWidgetItem* const source = availableList->currentItem ();

		if ( source == nullptr )
		{
			return;
		}

		const QString value      = source->data ( Qt::UserRole ).toString ();
		const bool    repeatable = !repeatableValue.isEmpty () && ( value == repeatableValue );

		// The repeatable option is COPIED, not moved: it is a structural mark rather than a thing there is one of.

		if ( !repeatable )
		{
			delete availableList->takeItem ( availableList->row ( source ) );
		}

		// Inserted AFTER the chosen list's current row rather than appended, so repeated adds build a run in order
		// instead of needing to be reordered afterwards.

		const int destination = ( chosenList->currentRow () >= 0 ) ? chosenList->currentRow () + 1 : chosenList->count ();

		chosenList->insertItem ( destination, build_item ( value ) );
		chosenList->setCurrentRow ( destination );
	}

	void TransferListEditor::handle_remove ()
	{
		const int row = chosenList->currentRow ();

		if ( row < 0 )
		{
			return;
		}

		QListWidgetItem* const item  = chosenList->takeItem ( row );
		const QString          value = item->data ( Qt::UserRole ).toString ();

		delete item;

		// A repeatable entry is deleted rather than returned -- it never left the available list to come back to it.

		if ( repeatableValue.isEmpty () || ( value != repeatableValue ) )
		{
			insert_available ( value );
		}

		chosenList->setCurrentRow ( qMin ( row, chosenList->count () - 1 ) );
	}

	void TransferListEditor::handle_move_up ()
	{
		const int row = chosenList->currentRow ();

		if ( row <= 0 )
		{
			return;
		}

		chosenList->insertItem ( row - 1, chosenList->takeItem ( row ) );
		chosenList->setCurrentRow ( row - 1 );
	}

	void TransferListEditor::handle_move_down ()
	{
		const int row = chosenList->currentRow ();

		if ( ( row < 0 ) || ( row >= chosenList->count () - 1 ) )
		{
			return;
		}

		chosenList->insertItem ( row + 1, chosenList->takeItem ( row ) );
		chosenList->setCurrentRow ( row + 1 );
	}

	void TransferListEditor::handle_restore_default ()
	{
		populate ( defaultChosen );

		// populate() rebuilds BOTH lists, and only the chosen one's model signals report themselves -- so the available
		// list's new contents, and the button states that read them, are settled here.

		handle_chosen_changed ();
	}

	void TransferListEditor::handle_chosen_changed ()
	{
		update_button_enablement ();

		emit chosen_changed ( chosen () );
	}

	void TransferListEditor::update_button_enablement ()
	{
		// Disabled, not hidden -- the rule the menus and the rest of the dialog follow.

		const int chosenRow = chosenList->currentRow ();

		addButton->setEnabled      ( availableList->currentItem () != nullptr );
		removeButton->setEnabled   ( chosenRow >= 0 );
		moveUpButton->setEnabled   ( chosenRow > 0 );
		moveDownButton->setEnabled ( ( chosenRow >= 0 ) && ( chosenRow < chosenList->count () - 1 ) );

		// Already at the shipped layout is worth SAYING, so the button reports it rather than doing nothing visibly.

		restoreButton->setEnabled ( chosen () != defaultChosen );
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	void TransferListEditor::insert_available ( const QString& value )
	{
		const int optionIndex = optionIndexByValue.value ( value, -1 );

		if ( optionIndex < 0 )
		{
			return;
		}

		// Back to its OPTION-ORDER position rather than appended: the available list must not depend on the history of
		// the session, or removing and re-adding the same command twice leaves it quietly rearranged.

		int destination = availableList->count ();

		for ( int row = 0; row < availableList->count (); ++row )
		{
			const QString other = availableList->item ( row )->data ( Qt::UserRole ).toString ();

			if ( optionIndexByValue.value ( other, -1 ) > optionIndex )
			{
				destination = row;

				break;
			}
		}

		availableList->insertItem ( destination, build_item ( value ) );
	}

	bool TransferListEditor::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( event->type () == QEvent::KeyPress )
		{
			const QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

			if ( ( keyEvent->key () == Qt::Key_Return ) || ( keyEvent->key () == Qt::Key_Enter ) )
			{
				// Enter transfers the current row in its natural direction, and the event is SWALLOWED: left to
				// propagate it would reach the dialog's default button and accept the whole Settings dialog on what the
				// user meant as one edit.

				if ( watched == availableList )
				{
					handle_add ();

					return true;
				}

				if ( watched == chosenList )
				{
					handle_remove ();

					return true;
				}
			}
		}

		return QWidget::eventFilter ( watched, event );
	}
}

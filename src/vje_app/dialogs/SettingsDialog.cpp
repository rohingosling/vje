//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   SettingsDialog implementation. See the header: this file knows field KINDS, never individual settings.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/SettingsDialog.hpp"

#include "AppConfig.hpp"
#include "dialogs/dialog_frame.hpp"
#include "dialogs/GroupBox.hpp"
#include "dialogs/TransferListEditor.hpp"
#include "services/IDialogService.hpp"
#include "services/ThemeService.hpp"
#include "style/theme_colour.hpp"
#include "style/tooltip_text.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// The one way a form of rows is made, boxed or not (SET-01c): labels left-aligned in their column, and EVERY
		// editor that is not fixed-width growing to the form's edge -- dropdowns, spin boxes, text boxes and the folder
		// field alike -- so a page's controls are one width rather than each its own. The row gap is stated rather than
		// left to the style.
		//-------------------------------------------------------------------------------------------------------------

		QFormLayout* new_row_form ()
		{
			QFormLayout* const form = new QFormLayout ();

			form->setLabelAlignment    ( Qt::AlignLeft | Qt::AlignVCenter );
			form->setFormAlignment     ( Qt::AlignLeft | Qt::AlignTop );
			form->setFieldGrowthPolicy ( QFormLayout::AllNonFixedFieldsGrow );
			form->setHorizontalSpacing ( config::settings_dialog::COLUMN_SPACING );
			form->setVerticalSpacing   ( config::settings_dialog::ROW_SPACING );
			form->setContentsMargins   ( 0, 0, 0, 0 );

			return form;
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	SettingsDialog::SettingsDialog
	(
		std::vector<SettingsGroup> groups,
		SettingsStore*             settings,
		ThemeService*              theme,
		IDialogService*            dialogs,
		const QIcon&               icon,
		QWidget*                   parent
	)
		: QDialog  ( parent )
		, settings ( settings )
		, theme    ( theme )
		, dialogs  ( dialogs )
		, groups   ( std::move ( groups ) )
		, snapshot ( this->groups, settings )
	{
		setWindowTitle ( tr ( "Settings" ) );
		resize ( config::settings_dialog::DEFAULT_WIDTH, config::settings_dialog::DEFAULT_HEIGHT );

		build_layout ( icon );
	}

	//=================================================================================================================
	// Construction Helpers
	//=================================================================================================================

	void SettingsDialog::build_layout ( const QIcon& icon )
	{
		groupList   = new QListWidget ( this );
		detailPages = new QStackedWidget ( this );

		groupList->setFixedWidth ( config::settings_dialog::MASTER_PANE_WIDTH );
		groupList->setAccessibleName ( tr ( "Settings groups" ) );

		for ( const SettingsGroup& group : groups )
		{
			groupList->addItem ( group.title );
		}

		// The master list drives the detail pane; that pairing IS the master-detail contract of SET-01.

		connect ( groupList, &QListWidget::currentRowChanged, detailPages, &QStackedWidget::setCurrentIndex );

		rebuild_pages ();

		groupList->setCurrentRow ( 0 );

		// RestoreDefaults carries Qt's ResetRole, which puts it at the opposite end of the box from OK / Cancel on every
		// platform -- a destructive-ish action should not sit where a confirming click lands by muscle memory.

		QDialogButtonBox* const buttons = new QDialogButtonBox
		(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
			this
		);

		connect ( buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::handle_accepted );
		connect ( buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject );

		if ( QPushButton* const restoreDefaults = buttons->button ( QDialogButtonBox::RestoreDefaults ) )
		{
			restoreDefaults->setAccessibleName ( tr ( "Restore factory defaults" ) );

			// Not the dialog's default button: Enter belongs to OK, and a reset must be asked for deliberately.

			restoreDefaults->setAutoDefault ( false );

			// Disabled-not-hidden, the rule the menus follow. Without the modal seam there is no way to ask for the
			// confirmation the reset requires, and a button that silently declined would be a dead control.

			restoreDefaults->setEnabled ( dialogs != nullptr );

			connect ( restoreDefaults, &QPushButton::clicked, this, &SettingsDialog::handle_restore_defaults );
		}

		// STYLE-15's content area: the master-detail pair. Margins are dialog_frame's, so the two panes sit at the same
		// inset every other dialog's content does.

		QWidget* const content = new QWidget ( this );

		QHBoxLayout* const panes = new QHBoxLayout ( content );

		panes->setContentsMargins ( 0, 0, 0, 0 );
		panes->addWidget ( groupList );
		panes->addWidget ( detailPages, 1 );

		apply_dialog_frame ( *this, content, buttons, icon );

		refresh_field_dependencies ();
	}

	void SettingsDialog::rebuild_pages ()
	{
		// Which group is open survives the rebuild -- a reset should leave the user looking at the page they pressed the
		// button on, not send them back to General.

		const int openGroup = ( groupList != nullptr ) ? groupList->currentRow () : -1;

		// The editors are children of their PAGE (a layout reparents whatever it is given), so deleting a page takes its
		// editors with it. The hash must therefore let go of them BEFORE the delete, not after, or it spends the loop
		// holding dangling pointers that refresh_field_dependencies would happily dereference.

		editorsByKey.clear ();
		labelsByKey.clear ();

		while ( detailPages->count () > 0 )
		{
			QWidget* const page = detailPages->widget ( 0 );

			detailPages->removeWidget ( page );

			// Deleted outright rather than deleteLater(): this runs from the Restore Defaults button, so no editor of
			// the page being destroyed is anywhere on the call stack, and a deferred delete would leave the old editors
			// alive -- still connected to the snapshot -- until the event loop next ran.

			delete page;
		}

		for ( const SettingsGroup& group : groups )
		{
			detailPages->addWidget ( build_page ( group ) );
		}

		if ( ( openGroup >= 0 ) && ( openGroup < detailPages->count () ) )
		{
			detailPages->setCurrentIndex ( openGroup );
		}

		align_label_column ();

		refresh_field_dependencies ();
	}

	QWidget* SettingsDialog::build_page ( const SettingsGroup& group )
	{
		QWidget* const page = new QWidget ( this );

		QVBoxLayout* const pageLayout = new QVBoxLayout ( page );

		pageLayout->setContentsMargins
		(
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN
		);

		pageLayout->setSpacing ( config::settings_dialog::SECTION_SPACING );

		bool pageHasSpanningEditor = false;

		for ( const SettingsSection& section : group.sections )
		{
			QFormLayout* const form = new_row_form ();

			// A composite editor is a PAGE, not a row (section 2.10): it takes the full width, carries its own captions
			// -- a label in the label column beside a pair of lists would name nothing in particular -- and takes the
			// page's spare HEIGHT, which a QFormLayout row cannot be made to do. So it is collected here and added to the
			// page layout rather than into the form. The schema puts one only in an UNTITLED section, so it is never cut
			// off from a box it was meant to be in (tst_settings_schema).

			QList<QWidget*> spanningEditors;

			for ( const SettingsField& field : section.fields )
			{
				QWidget* const editor = build_editor ( field );

				if ( editor == nullptr )
				{
					continue;
				}

				editorsByKey.insert ( field.key, editor );

				if ( settings_field_spans_page ( field.kind ) )
				{
					spanningEditors.append ( editor );
				}
				else
				{
					add_row ( *form, page, field, editor );
				}
			}

			// A titled section is a box (SET-01c); an untitled one puts its rows on the page itself. Setting the form on
			// the box, or adding it to the page, reparents every row into place.

			if ( !section.title.isEmpty () )
			{
				GroupBox* const box = new GroupBox ( section.title, page );

				box->setLayout ( form );

				pageLayout->addWidget ( box );
			}
			else if ( form->rowCount () > 0 )
			{
				pageLayout->addLayout ( form );
			}
			else
			{
				delete form;                                       // Nothing but a spanning editor: no rows to lay out.
			}

			for ( QWidget* const editor : spanningEditors )
			{
				pageLayout->addWidget ( editor, 1 );

				pageHasSpanningEditor = true;
			}
		}

		if ( !pageHasSpanningEditor )
		{
			pageLayout->addStretch ( 1 );                          // Boxes sit at the top rather than spreading down it.
		}

		return page;
	}

	void SettingsDialog::add_row ( QFormLayout& form, QWidget* page, const SettingsField& field, QWidget* editor )
	{
		// The label is CONSTRUCTED here rather than left to addRow's QString overload, which builds one internally and
		// hands back no pointer to it. SET-01b needs it greyed out with its editor, and QFormLayout will not do that on its
		// own -- the label is the field's sibling, and QWidget's enabled state propagates to children only. labelForField()
		// could find it back, but that would mean keeping the QFormLayout alive to ask, which is a longer-lived pointer
		// than the label itself.

		QLabel* const label = new QLabel ( field.label, page );

		// The buddy is what gives the label's &accelerator (if one is ever added) somewhere to send focus, and what lets a
		// screen reader read the pair as one row (NFR-05).

		label->setBuddy ( editor );

		// STYLE-17: what the setting does, on the label and the editor alike, since either is where a pointer comes to
		// rest while the user wonders. The editor may be a composite (the log folder's field and Browse button); a
		// child without a tooltip of its own passes the question up to it, so one call covers the row.

		const QString tooltip = tooltip_text ( field.description );

		label ->setToolTip ( tooltip );
		editor->setToolTip ( tooltip );

		labelsByKey.insert ( field.key, label );

		form.addRow ( label, editor );
	}

	void SettingsDialog::align_label_column ()
	{
		// The widest label on ANY page sets the column for every page (SET-01c). Each box is a form of its own, and a form
		// sizes its label column to its own labels -- so left alone, every box would start its editors somewhere
		// different. Every box is the same width, so equal label columns give every editor in the dialog one left edge
		// and one width.
		//
		// Measured from the labels rather than stated as a number, so it holds whatever the font, the display scaling or
		// the language makes of them. The minimum widths set last time are cleared first, or a label could never report
		// a width narrower than the column it was given.

		int widest = 0;

		for ( QLabel* const label : std::as_const ( labelsByKey ) )
		{
			label->setMinimumWidth ( 0 );

			widest = std::max ( widest, label->sizeHint ().width () );
		}

		for ( QLabel* const label : std::as_const ( labelsByKey ) )
		{
			label->setMinimumWidth ( widest );
		}
	}

	QWidget* SettingsDialog::build_editor ( const SettingsField& field )
	{
		// Every editor writes to the SNAPSHOT. Nothing here touches the store, which is what makes Cancel free (SET-01).

		switch ( field.kind )
		{
			case SettingsFieldKind::Choice:
			{
				QComboBox* const combo = new QComboBox ( this );

				for ( const SettingsOption& option : field.options )
				{
					combo->addItem ( option.label, option.value );
				}

				const int storedIndex = combo->findData ( snapshot.value_string ( field.key ) );

				combo->setCurrentIndex ( ( storedIndex >= 0 ) ? storedIndex : 0 );
				combo->setAccessibleName ( field.label );

				connect ( combo, &QComboBox::currentIndexChanged, this, [ this, combo, field ] ( int index )
				{
					snapshot.set_string ( field.key, combo->itemData ( index ).toString () );

					refresh_field_dependencies ();
				} );

				return combo;
			}

			case SettingsFieldKind::YesNo:
			{
				// A Yes / No dropdown rather than a check box, which is how section 2.10's sketch shows these.

				QComboBox* const combo = new QComboBox ( this );

				combo->addItem ( tr ( "Yes" ), true );
				combo->addItem ( tr ( "No" ),  false );

				combo->setCurrentIndex ( snapshot.value_bool ( field.key ) ? 0 : 1 );
				combo->setAccessibleName ( field.label );

				connect ( combo, &QComboBox::currentIndexChanged, this, [ this, combo, field ] ( int index )
				{
					snapshot.set_bool ( field.key, combo->itemData ( index ).toBool () );

					refresh_field_dependencies ();
				} );

				return combo;
			}

			case SettingsFieldKind::CheckBox:
			{
				// The label is the form's, so the box itself carries no text -- the check boxes then line up in the value
				// column with every other editor (SET-04's list).

				QCheckBox* const checkBox = new QCheckBox ( this );

				checkBox->setChecked ( snapshot.value_bool ( field.key ) );
				checkBox->setAccessibleName ( field.label );

				connect ( checkBox, &QCheckBox::toggled, this, [ this, field ] ( bool checked )
				{
					snapshot.set_bool ( field.key, checked );

					refresh_field_dependencies ();
				} );

				return checkBox;
			}

			case SettingsFieldKind::Integer:
			{
				QSpinBox* const spinBox = new QSpinBox ( this );

				spinBox->setRange ( field.minimumInteger, field.maximumInteger );
				spinBox->setValue ( snapshot.value_int ( field.key ) );
				spinBox->setAccessibleName ( field.label );

				connect ( spinBox, &QSpinBox::valueChanged, this, [ this, field ] ( int value )
				{
					snapshot.set_int ( field.key, value );
				} );

				return spinBox;
			}

			case SettingsFieldKind::ShortText:
			{
				QLineEdit* const lineEdit = new QLineEdit ( snapshot.value_string ( field.key ), this );

				if ( field.maximumLength > 0 )
				{
					lineEdit->setMaxLength ( field.maximumLength );
				}

				lineEdit->setPlaceholderText ( field.placeholder );
				lineEdit->setAccessibleName ( field.label );

				connect ( lineEdit, &QLineEdit::textChanged, this, [ this, field ] ( const QString& text )
				{
					snapshot.set_string ( field.key, text );
				} );

				return lineEdit;
			}

			case SettingsFieldKind::TransferList:
			{
				// The only composite kind (SET-04). Like every other editor it writes to the SNAPSHOT and never to the
				// store, which is what makes Cancel free -- and is why the toolbar does not update live while the
				// dialog is open.

				TransferListEditor* const transferList = new TransferListEditor ( field, snapshot.value_string_list ( field.key ), this );

				connect ( transferList, &TransferListEditor::chosen_changed, this, [ this, field ] ( const QStringList& chosen )
				{
					snapshot.set_string_list ( field.key, chosen );
				} );

				return transferList;
			}

			case SettingsFieldKind::Folder:
			{
				// A line edit and a Browse button in one container, so the pair enables and disables as one field
				// (SET-09's dependency on diagnostic logging).

				QWidget* const container = new QWidget ( this );

				QLineEdit*   const lineEdit = new QLineEdit ( snapshot.value_string ( field.key ), container );
				QPushButton* const browse   = new QPushButton ( tr ( "Browse..." ), container );

				lineEdit->setPlaceholderText ( field.placeholder );
				lineEdit->setAccessibleName ( field.label );

				connect ( lineEdit, &QLineEdit::textChanged, this, [ this, field ] ( const QString& text )
				{
					snapshot.set_string ( field.key, text );
				} );

				connect ( browse, &QPushButton::clicked, this, [ this, field, lineEdit ] ()
				{
					if ( dialogs == nullptr )
					{
						return;
					}

					// Through the same modal seam as every other dialog in the application (IDialogService).

					const QString chosen = dialogs->choose_folder ( tr ( "Log Folder" ), lineEdit->text () );

					if ( !chosen.isEmpty () )
					{
						lineEdit->setText ( chosen );          // Its textChanged writes the snapshot.
					}
				} );

				QHBoxLayout* const containerLayout = new QHBoxLayout ( container );

				containerLayout->setContentsMargins ( 0, 0, 0, 0 );
				containerLayout->addWidget ( lineEdit, 1 );
				containerLayout->addWidget ( browse );

				return container;
			}

			case SettingsFieldKind::Colour:
			{
				// SET-14a. The Folder row's shape -- a text box filling the row and a button beside it -- holding a CSS
				// hexadecimal colour.
				//
				// THE BOX SPEAKS THE THEME SHOWING NOW. The snapshot holds the Dark theme's colour, as the store does; the
				// box shows it as the dot looks in the theme in effect when the dialog opened, and takes a value typed or
				// picked as that theme's. The conversion is its own inverse (style/theme_colour), so one function goes
				// both ways.

				QWidget* const container = new QWidget ( this );

				QLineEdit*   const lineEdit = new QLineEdit ( container );
				QPushButton* const choose   = new QPushButton ( tr ( "Choose..." ), container );

				const bool   dark     = ( theme != nullptr ) && theme->is_dark_effective ();
				const QColor fallback = parse_hex_colour ( field.defaultValue.toString () ).value_or ( QColor ( 0x80, 0x80, 0x80 ) );

				// The last complete value, as the box shows it: what it is seeded with, and what an incomplete value
				// returns to.

				auto shown_value = [ this, field, dark, fallback ] ()
				{
					return hex_colour ( colour_for_theme ( parse_hex_colour ( snapshot.value_string ( field.key ) ).value_or ( fallback ), dark ) );
				};

				lineEdit->setText ( shown_value () );
				lineEdit->setAccessibleName ( field.label );

				// Hex digits and a leading '#', at most six: what cannot become a colour cannot be typed. What can but is
				// not one yet ("#12") is allowed while typing, and never reaches the snapshot.

				lineEdit->setValidator
				(
					new QRegularExpressionValidator ( QRegularExpression ( QStringLiteral ( "#?[0-9A-Fa-f]{0,6}" ) ), lineEdit )
				);

				connect ( lineEdit, &QLineEdit::textChanged, this, [ this, field, dark ] ( const QString& text )
				{
					const std::optional<QColor> typed = parse_hex_colour ( text );

					if ( typed.has_value () )
					{
						snapshot.set_string ( field.key, hex_colour ( colour_for_theme ( *typed, dark ) ) );
					}
				} );

				// On leaving the box: a complete value is written back in the one form, "#RRGGBB" in upper case, and an
				// incomplete one returns to the last complete value -- so the box never shows a value OK would not apply.

				connect ( lineEdit, &QLineEdit::editingFinished, this, [ lineEdit, shown_value ] ()
				{
					lineEdit->setText ( shown_value () );
				} );

				connect ( choose, &QPushButton::clicked, this, [ this, field, lineEdit, shown_value ] ()
				{
					if ( dialogs == nullptr )
					{
						return;
					}

					// Opened on the colour the box shows, through the modal seam (IDialogService).

					const QColor initial = parse_hex_colour ( lineEdit->text () ).value_or ( *parse_hex_colour ( shown_value () ) );
					const QColor picked  = dialogs->choose_colour ( field.label, initial );

					if ( picked.isValid () )
					{
						lineEdit->setText ( hex_colour ( picked ) );          // Its textChanged writes the snapshot.
					}
				} );

				QHBoxLayout* const containerLayout = new QHBoxLayout ( container );

				containerLayout->setContentsMargins ( 0, 0, 0, 0 );
				containerLayout->addWidget ( lineEdit, 1 );
				containerLayout->addWidget ( choose );

				return container;
			}
		}

		return nullptr;
	}

	void SettingsDialog::refresh_field_dependencies ()
	{
		// Disabled, not hidden -- the same rule the menus follow: a setting that exists but does not apply yet should say
		// so, not vanish (SET-09).
		//
		// THE WHOLE ROW GREYS OUT, label included (SET-01b). Qt does not do this for us: QFormLayout puts the label and
		// the field in adjacent columns as siblings, and QWidget::setEnabled propagates down to children rather than
		// across, so a disabled editor leaves a fully-black label beside it. That reads as a rendering fault rather
		// than as a state -- and the label is the half that says WHAT is unavailable, so it is the half most worth
		// greying. It also carries the state to assistive technology, which reads the label for the row's name.

		for ( const SettingsField* const field : settings_fields ( groups ) )
		{
			const bool enabled = snapshot.is_field_enabled ( *field );

			QWidget* const editor = editorsByKey.value ( field->key, nullptr );

			if ( editor != nullptr )
			{
				editor->setEnabled ( enabled );
			}

			// Absent for the composite kinds, which carry their own captions inside the widget and so grey them with it
			// -- value() answering null is the ordinary case there, not a missed row.

			QLabel* const label = labelsByKey.value ( field->key, nullptr );

			if ( label != nullptr )
			{
				label->setEnabled ( enabled );
			}
		}
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void SettingsDialog::handle_accepted ()
	{
		const QStringList changedKeys = snapshot.apply ( settings );

		// The theme is the one setting that also has to REPAINT something, and ThemeService owns that (section 2.9): it is
		// the same setting as View > Theme, mirrored, so it is applied through the service rather than left as a written
		// key nothing acted on (SET-03).

		if ( ( theme != nullptr ) && changedKeys.contains ( settings_keys::THEME ) )
		{
			theme->set_theme ( ThemeService::theme_from_string ( snapshot.value_string ( settings_keys::THEME ), theme->theme () ) );
		}

		accept ();
	}

	void SettingsDialog::handle_restore_defaults ()
	{
		if ( dialogs == nullptr )
		{
			return;
		}

		const bool confirmed = dialogs->confirm
		(
			tr ( "Restore Defaults" ),
			tr
			(
				"Restore all settings to their factory defaults?\n\n"
				"This affects every group, including changes made on pages that are not currently open. "
				"Nothing is written until you choose OK."
			)
		);

		if ( !confirmed )
		{
			return;
		}

		// Seeding a snapshot from a NULL store is already defined as "every field at its schema default" -- the state a
		// first run is in. Reusing it is what keeps the reset from becoming a second, hand-written list of defaults that
		// could drift from the schema's.

		snapshot = SettingsSnapshot ( groups, nullptr );

		rebuild_pages ();
	}
}

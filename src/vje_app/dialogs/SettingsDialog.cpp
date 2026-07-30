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
#include "dialogs/TransferListEditor.hpp"
#include "services/IDialogService.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	SettingsDialog::SettingsDialog
	(
		std::vector<SettingsGroup> groups,
		SettingsStore*             settings,
		ThemeService*              theme,
		IDialogService*            dialogs,
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
		setModal ( true );
		resize ( config::settings_dialog::DEFAULT_WIDTH, config::settings_dialog::DEFAULT_HEIGHT );

		build_layout ();
	}

	//=================================================================================================================
	// Construction Helpers
	//=================================================================================================================

	void SettingsDialog::build_layout ()
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

		QHBoxLayout* const panes = new QHBoxLayout ();

		panes->addWidget ( groupList );
		panes->addWidget ( detailPages, 1 );

		QVBoxLayout* const outerLayout = new QVBoxLayout ( this );

		outerLayout->addLayout ( panes, 1 );
		outerLayout->addWidget ( buttons );

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

		refresh_field_dependencies ();
	}

	QWidget* SettingsDialog::build_page ( const SettingsGroup& group )
	{
		QWidget* const page = new QWidget ( this );

		QFormLayout* const form = new QFormLayout ();

		// Labels left-aligned in a label column, editors left-aligned in a value column (section 2.10). Without
		// FieldsStayAtSizeHint a combo box would stretch to the pane's width, which is not "left-aligned in a column".

		form->setLabelAlignment ( Qt::AlignLeft | Qt::AlignVCenter );
		form->setFormAlignment ( Qt::AlignLeft | Qt::AlignTop );
		form->setFieldGrowthPolicy ( QFormLayout::FieldsStayAtSizeHint );
		form->setHorizontalSpacing ( config::settings_dialog::COLUMN_SPACING );

		// A composite editor is a PAGE, not a row (section 2.10): it takes the full width, carries its own captions --
		// a label in the label column beside a pair of lists would name nothing in particular -- and takes the page's
		// spare HEIGHT, which a QFormLayout row cannot be made to do. So it is collected here and added to the page
		// layout below the form rather than into it.

		QList<QWidget*> spanningEditors;

		for ( const SettingsField& field : group.fields )
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
				form->addRow ( field.label, editor );
			}
		}

		QVBoxLayout* const pageLayout = new QVBoxLayout ( page );

		pageLayout->setContentsMargins
		(
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN,
			config::settings_dialog::PAGE_MARGIN
		);

		pageLayout->addLayout ( form );

		for ( QWidget* const editor : spanningEditors )
		{
			pageLayout->addWidget ( editor, 1 );
		}

		if ( spanningEditors.isEmpty () )
		{
			pageLayout->addStretch ( 1 );                          // Rows sit at the top rather than spreading down it.
		}

		return page;
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
		}

		return nullptr;
	}

	void SettingsDialog::refresh_field_dependencies ()
	{
		// Disabled, not hidden -- the same rule the menus follow: a setting that exists but does not apply yet should say
		// so, not vanish (SET-09).

		for ( const SettingsGroup& group : groups )
		{
			for ( const SettingsField& field : group.fields )
			{
				QWidget* const editor = editorsByKey.value ( field.key, nullptr );

				if ( editor != nullptr )
				{
					editor->setEnabled ( snapshot.is_field_enabled ( field ) );
				}
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

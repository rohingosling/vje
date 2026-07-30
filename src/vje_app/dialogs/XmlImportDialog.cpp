//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImportDialog implementation. See the header for why the dialog decides nothing and why only the key field's
//   re-render is coalesced.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/XmlImportDialog.hpp"

#include "AppConfig.hpp"
#include "controllers/XmlImportController.hpp"
#include "style/CodeTokenPalette.hpp"
#include "views/CodeEditor.hpp"
#include "views/JsonHighlighter.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	XmlImportDialog::XmlImportDialog ( XmlImportController* controller, const QString& fileName, QWidget* parent )
	:	QDialog    ( parent ),
		controller ( controller )
	{
		setWindowTitle ( tr ( "Import XML to JSON" ) );
		setModal ( true );

		// No context-help button in the title bar; there is no help topic behind it (the GoToDialog rule).

		setWindowFlags ( windowFlags () & ~Qt::WindowContextHelpButtonHint );

		//-------------------------------------------------------------------------------------------------------------
		// The file being imported. A read-only field rather than a label, so the name can be selected and copied -- it
		// is the one piece of the dialog a user may want to paste somewhere when a file will not convert.
		//-------------------------------------------------------------------------------------------------------------

		QLabel* const fileLabel = new QLabel ( tr ( "&File:" ), this );
		QLineEdit* const fileField = new QLineEdit ( fileName, this );

		fileField->setReadOnly ( true );
		fileLabel->setBuddy ( fileField );

		QHBoxLayout* const fileRow = new QHBoxLayout ();

		fileRow->setSpacing ( config::xml_import::ROW_SPACING );
		fileRow->addWidget ( fileLabel );
		fileRow->addWidget ( fileField, 1 );

		//-------------------------------------------------------------------------------------------------------------
		// The options column.
		//-------------------------------------------------------------------------------------------------------------

		QLabel* const strategyLabel = new QLabel ( tr ( "&Conversion strategy:" ), this );

		strategyList = new QListWidget ( this );

		strategyList->setSelectionMode ( QAbstractItemView::SingleSelection );
		strategyList->setMinimumHeight ( config::xml_import::STRATEGY_LIST_MINIMUM_HEIGHT );
		strategyLabel->setBuddy ( strategyList );

		build_strategy_list ();

		inferScalarsBox = new QCheckBox ( tr ( "&Infer scalar types" ), this );

		inferScalarsBox->setChecked ( controller->infer_scalar_types () );

		inferScalarsBox->setToolTip
		(
			tr ( "Convert null, true, false and whole numbers to typed JSON values. Decimals and exponents stay strings, "
			     "so a version like 1.0 survives as text." )
		);

		textValueKeyLabel = new QLabel ( tr ( "Te&xt value key:" ), this );
		textValueKeyField = new QLineEdit ( controller->text_value_key (), this );

		textValueKeyField->setPlaceholderText ( tr ( "The element's own name" ) );
		textValueKeyLabel->setBuddy ( textValueKeyField );

		QVBoxLayout* const optionsColumn = new QVBoxLayout ();

		optionsColumn->setSpacing ( config::xml_import::ROW_SPACING );
		optionsColumn->addWidget ( strategyLabel );
		optionsColumn->addWidget ( strategyList, 1 );
		optionsColumn->addWidget ( inferScalarsBox );
		optionsColumn->addWidget ( textValueKeyLabel );
		optionsColumn->addWidget ( textValueKeyField );

		//-------------------------------------------------------------------------------------------------------------
		// The preview column. The notes sit UNDER the preview and inside this column, so a file that produces four
		// warnings shortens the preview rather than growing the dialog under the user's cursor.
		//-------------------------------------------------------------------------------------------------------------

		QLabel* const previewLabel = new QLabel ( tr ( "Preview:" ), this );

		previewEditor = new CodeEditor ( this );

		// NFR-05. "Preview:" labels it on screen but is not a buddy -- a read-only editor takes no mnemonic focus --
		// so the control carries the name itself.

		previewEditor->setAccessibleName ( tr ( "Conversion preview" ) );

		previewEditor->setReadOnly ( true );

		highlighter = new JsonHighlighter ( previewEditor->document () );

		notesLabel = new QLabel ( this );

		notesLabel->setWordWrap ( true );
		notesLabel->setTextInteractionFlags ( Qt::TextSelectableByMouse );

		QVBoxLayout* const previewColumn = new QVBoxLayout ();

		previewColumn->setSpacing ( config::xml_import::ROW_SPACING );
		previewColumn->addWidget ( previewLabel );
		previewColumn->addWidget ( previewEditor, 1 );
		previewColumn->addWidget ( notesLabel );

		QHBoxLayout* const bodyRow = new QHBoxLayout ();

		bodyRow->setSpacing ( config::xml_import::ROW_SPACING );
		bodyRow->addLayout ( optionsColumn, config::xml_import::OPTIONS_STRETCH );
		bodyRow->addLayout ( previewColumn, config::xml_import::PREVIEW_STRETCH );

		//-------------------------------------------------------------------------------------------------------------
		// Buttons. Import is named rather than left as "OK", because the dialog's whole content is a decision about
		// what an import will produce and "OK" says nothing about which of the two buttons commits it.
		//-------------------------------------------------------------------------------------------------------------

		QDialogButtonBox* const buttonBox = new QDialogButtonBox ( QDialogButtonBox::Cancel, this );

		importButton = buttonBox->addButton ( tr ( "&Import" ), QDialogButtonBox::AcceptRole );

		importButton->setDefault ( true );

		QVBoxLayout* const dialogLayout = new QVBoxLayout ( this );

		dialogLayout->setContentsMargins
		(
			config::xml_import::CONTENT_MARGIN, config::xml_import::CONTENT_MARGIN,
			config::xml_import::CONTENT_MARGIN, config::xml_import::CONTENT_MARGIN
		);

		dialogLayout->setSpacing ( config::xml_import::ROW_SPACING );

		dialogLayout->addLayout ( fileRow );
		dialogLayout->addLayout ( bodyRow, 1 );
		dialogLayout->addWidget ( buttonBox );

		resize ( config::xml_import::DEFAULT_WIDTH, config::xml_import::DEFAULT_HEIGHT );

		//-------------------------------------------------------------------------------------------------------------
		// Wiring.
		//-------------------------------------------------------------------------------------------------------------

		typingTimer = new QTimer ( this );

		typingTimer->setSingleShot ( true );
		typingTimer->setInterval ( config::xml_import::PREVIEW_TYPING_DELAY );

		connect ( strategyList,      &QListWidget::currentRowChanged, this, &XmlImportDialog::handle_strategy_changed );
		connect ( inferScalarsBox,   &QCheckBox::toggled,             this, &XmlImportDialog::handle_infer_toggled );
		connect ( textValueKeyField, &QLineEdit::textEdited,          this, &XmlImportDialog::handle_text_key_edited );
		connect ( typingTimer,       &QTimer::timeout,                this, &XmlImportDialog::refresh_preview );

		connect ( buttonBox, &QDialogButtonBox::accepted, this, &XmlImportDialog::accept );
		connect ( buttonBox, &QDialogButtonBox::rejected, this, &XmlImportDialog::reject );

		update_text_key_enablement ();
		refresh_preview ();

		strategyList->setFocus ( Qt::OtherFocusReason );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QListWidget* XmlImportDialog::strategy_list () const
	{
		return strategyList;
	}

	QCheckBox* XmlImportDialog::infer_scalars_box () const
	{
		return inferScalarsBox;
	}

	QLineEdit* XmlImportDialog::text_value_key_field () const
	{
		return textValueKeyField;
	}

	CodeEditor* XmlImportDialog::preview_editor () const
	{
		return previewEditor;
	}

	QLabel* XmlImportDialog::notes_label () const
	{
		return notesLabel;
	}

	QPushButton* XmlImportDialog::import_button () const
	{
		return importButton;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void XmlImportDialog::handle_strategy_changed ()
	{
		controller->select_index ( strategyList->currentRow () );

		update_text_key_enablement ();
		refresh_preview ();
	}

	void XmlImportDialog::handle_infer_toggled ( bool infer )
	{
		controller->set_infer_scalar_types ( infer );

		refresh_preview ();
	}

	void XmlImportDialog::handle_text_key_edited ()
	{
		// The VALUE reaches the controller on the keystroke and only the RE-RENDER waits: a user who types a key and
		// presses Import immediately must import with the key they typed, not with the one the last render used.

		controller->set_text_value_key ( textValueKeyField->text () );

		typingTimer->start ();
	}

	void XmlImportDialog::refresh_preview ()
	{
		typingTimer->stop ();

		const XmlImportPreview& preview = controller->preview ();

		previewEditor->set_text_preserving_view ( preview.text );

		// A freshly set buffer carries no highlighting until QSyntaxHighlighter's own zero-timer runs, and the dialog
		// may be measured or shown before then (lessons-learned Q16).

		highlighter->rehighlight ();

		// The notes, in one label: the parse failure if there was one, then the truncation note, then the
		// degraded-construct warnings. Blank when there is nothing to say, which is most of the time.

		QStringList notes;

		if ( !preview.error.isEmpty () )
		{
			notes.append ( tr ( "This file could not be read as XML: %1" ).arg ( preview.error ) );
		}

		if ( !preview.truncationNote.isEmpty () )
		{
			notes.append ( preview.truncationNote );
		}

		notes.append ( preview.warnings );

		notesLabel->setText ( notes.join ( QStringLiteral ( "\n" ) ) );

		importButton->setEnabled ( controller->can_import () );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void XmlImportDialog::build_strategy_list ()
	{
		// Two lines per row -- the name, then the one-line description section 2.11 asks each row to carry. The
		// description is also the tooltip, so a narrow dialog that elides it still answers a hover.

		for ( const XmlStrategyChoice& choice : XmlImportController::strategy_choices () )
		{
			QListWidgetItem* const item = new QListWidgetItem
			(
				choice.labelled_name () + QStringLiteral ( "\n" ) + choice.description,
				strategyList
			);

			item->setToolTip ( choice.description );
		}

		// Preselection is the persisted last choice (SET-08); with nothing stored that is the recommended strategy,
		// because that is what an absent setting reads back as.

		strategyList->setCurrentRow ( controller->selected_index () );
	}

	void XmlImportDialog::update_text_key_enablement ()
	{
		const bool applies = controller->text_value_key_applies ();

		textValueKeyLabel->setEnabled ( applies );
		textValueKeyField->setEnabled ( applies );
	}
}

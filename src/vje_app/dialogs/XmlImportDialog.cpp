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
#include "dialogs/dialog_frame.hpp"
#include "style/CodeTokenPalette.hpp"
#include "style/dialog_surface.hpp"
#include "style/tooltip_text.hpp"
#include "views/CodeEditor.hpp"
#include "views/JsonHighlighter.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	XmlImportDialog::XmlImportDialog
	(
		XmlImportController* controller,
		const QString&       fileName,
		const QIcon&         icon,
		QWidget*             parent
	)
	:	QDialog    ( parent ),
		controller ( controller )
	{
		setWindowTitle ( tr ( "Import XML to JSON" ) );

		//-------------------------------------------------------------------------------------------------------------
		// The file being imported. A read-only field rather than a label, so the name can be selected and copied -- it
		// is the one piece of the dialog a user may want to paste somewhere when a file will not convert.
		//-------------------------------------------------------------------------------------------------------------

		QLabel* const fileLabel = new QLabel ( tr ( "&File:" ), this );
		QLineEdit* const fileField = new QLineEdit ( fileName, this );

		fileField->setReadOnly ( true );
		fileLabel->setBuddy ( fileField );

		QHBoxLayout* const fileRow = new QHBoxLayout ();

		fileRow->setSpacing ( config::dialog::ROW_SPACING );
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

		// The selected strategy's description, in its own labelled block (section 2.11). Dimmed through
		// style/dialog_surface rather than through QPalette::PlaceholderText, whose sub-AA light value is accepted for
		// text that prompts and is replaced -- this is prose the user has to READ to choose between four strategies.

		QLabel* const descriptionCaption = new QLabel ( tr ( "Description:" ), this );

		descriptionLabel = new QLabel ( this );

		descriptionLabel->setWordWrap ( true );
		descriptionLabel->setTextInteractionFlags ( Qt::TextSelectableByMouse );
		descriptionLabel->setAlignment ( Qt::AlignTop | Qt::AlignLeft );

		// Reserved from the start so the column does not shift as the user arrows down the list: a two-line
		// description following a one-line one would otherwise move everything below it.

		descriptionLabel->setMinimumHeight ( config::xml_import::DESCRIPTION_MINIMUM_HEIGHT );

		// NFR-05. The caption names it on screen but cannot be a buddy -- a label takes no focus -- so the prose
		// carries the name itself.

		descriptionLabel->setAccessibleName ( tr ( "Conversion strategy description" ) );

		inferScalarsBox = new QCheckBox ( tr ( "&Infer scalar types" ), this );

		inferScalarsBox->setChecked ( controller->infer_scalar_types () );

		inferScalarsBox->setToolTip
		(
			tooltip_text
			(
				tr ( "Convert null, true, false and whole numbers to typed JSON values. Decimals and exponents stay strings, "
				     "so a version like 1.0 survives as text." )
			)
		);

		textValueKeyLabel = new QLabel ( tr ( "Te&xt value key:" ), this );
		textValueKeyField = new QLineEdit ( controller->text_value_key (), this );

		textValueKeyField->setPlaceholderText ( tr ( "The element's own name" ) );
		textValueKeyLabel->setBuddy ( textValueKeyField );

		QVBoxLayout* const optionsColumn = new QVBoxLayout ();

		optionsColumn->setSpacing ( config::dialog::ROW_SPACING );
		optionsColumn->addWidget ( strategyLabel );
		optionsColumn->addWidget ( strategyList, 1 );
		optionsColumn->addWidget ( descriptionCaption );
		optionsColumn->addWidget ( descriptionLabel );
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

		// The notes, under their own caption and as a BULLET LIST. There are up to six independent things here -- a
		// parse failure, a truncation note and the four degraded-construct warnings (section 2.11) -- and run together
		// as newline-separated sentences they read as one paragraph, so a user who has two problems sees one. The
		// caption is styled like the strategy Description's: ordinary window text naming the block beneath it.

		notesCaption = new QLabel ( tr ( "Notes:" ), this );

		notesLabel = new QLabel ( this );

		// Rich text, because a wrapped note has to hang under its own bullet rather than return to the left margin --
		// which is the whole reason a bullet list is clearer than a "- " prefix here. Every note is HTML-ESCAPED
		// before it goes in: these carry the XML parser's own message and the user's key names, and a '<' from an
		// unclosed tag would otherwise be swallowed as markup.

		notesLabel->setTextFormat ( Qt::RichText );
		notesLabel->setWordWrap ( true );
		notesLabel->setTextInteractionFlags ( Qt::TextSelectableByMouse );

		// NFR-05. The caption names it on screen but cannot be a buddy, so the prose carries the name itself.

		notesLabel->setAccessibleName ( tr ( "Conversion notes" ) );

		QVBoxLayout* const previewColumn = new QVBoxLayout ();

		previewColumn->setSpacing ( config::dialog::ROW_SPACING );
		previewColumn->addWidget ( previewLabel );
		previewColumn->addWidget ( previewEditor, 1 );
		previewColumn->addWidget ( notesCaption );
		previewColumn->addWidget ( notesLabel );

		QHBoxLayout* const bodyRow = new QHBoxLayout ();

		bodyRow->setSpacing ( config::dialog::ROW_SPACING );
		bodyRow->addLayout ( optionsColumn, config::xml_import::OPTIONS_STRETCH );
		bodyRow->addLayout ( previewColumn, config::xml_import::PREVIEW_STRETCH );

		//-------------------------------------------------------------------------------------------------------------
		// Buttons. Import is named rather than left as "OK", because the dialog's whole content is a decision about
		// what an import will produce and "OK" says nothing about which of the two buttons commits it.
		//-------------------------------------------------------------------------------------------------------------

		QDialogButtonBox* const buttonBox = new QDialogButtonBox ( QDialogButtonBox::Cancel, this );

		importButton = buttonBox->addButton ( tr ( "&Import" ), QDialogButtonBox::AcceptRole );

		importButton->setDefault ( true );

		// STYLE-15's content area: the file row above the two columns. No margins of its own -- the inset is
		// dialog_frame's.

		QWidget* const content = new QWidget ( this );

		QVBoxLayout* const contentLayout = new QVBoxLayout ( content );

		contentLayout->setContentsMargins ( 0, 0, 0, 0 );
		contentLayout->setSpacing ( config::dialog::ROW_SPACING );

		contentLayout->addLayout ( fileRow );
		contentLayout->addLayout ( bodyRow, 1 );

		apply_dialog_frame ( *this, content, buttonBox, icon );

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

		// After both prose blocks exist, not beside either of them: it tones the pair, so calling it from the first
		// one's construction would leave the second at ordinary text colour until the next theme change.

		apply_prose_tone ();

		update_description ();
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

	QLabel* XmlImportDialog::description_label () const
	{
		return descriptionLabel;
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

	QLabel* XmlImportDialog::notes_caption () const
	{
		return notesCaption;
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

		update_description ();
		update_text_key_enablement ();
		refresh_preview ();
	}

	void XmlImportDialog::changeEvent ( QEvent* event )
	{
		QDialog::changeEvent ( event );

		// The dimmed prose is a distance from the dialog's surface, so it has to be recomputed whenever that surface
		// moves. Setting the label's own palette does not re-enter here: a widget palette change is delivered to the
		// LABEL, not to this dialog.

		if ( ( event->type () == QEvent::PaletteChange ) || ( event->type () == QEvent::StyleChange ) )
		{
			apply_prose_tone ();
		}
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

		notesLabel->setText ( notes_html ( notes ) );

		// The caption goes with its content: a "Notes:" heading over nothing says a section is missing rather than
		// empty. Hiding both gives the space back to the preview, which is what the column's stretch is for.

		notesCaption->setVisible ( !notes.isEmpty () );
		notesLabel  ->setVisible ( !notes.isEmpty () );

		importButton->setEnabled ( controller->can_import () );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void XmlImportDialog::build_strategy_list ()
	{
		// ONE line per row -- the name alone (section 2.11, revised 2026-08-06). The description was a second line in
		// every row, which made the list four two-line rows and left the prose no room to be prose; it now appears
		// once, beneath the list, for the row that is selected. The tooltip stays, so a narrow dialog that elides a
		// long name still answers a hover.

		for ( const XmlStrategyChoice& choice : XmlImportController::strategy_choices () )
		{
			QListWidgetItem* const item = new QListWidgetItem ( choice.labelled_name (), strategyList );

			item->setToolTip ( tooltip_text ( choice.description ) );
		}

		// Preselection is the persisted last choice (SET-08); with nothing stored that is the recommended strategy,
		// because that is what an absent setting reads back as.

		strategyList->setCurrentRow ( controller->selected_index () );
	}

	void XmlImportDialog::update_description ()
	{
		// Read from the CONTROLLER's selection rather than from the list row, so the block always describes the
		// strategy the preview was generated with -- the two cannot drift apart, because there is only one of them.

		const std::vector<XmlStrategyChoice> choices = XmlImportController::strategy_choices ();

		const int selected = controller->selected_index ();

		descriptionLabel->setText
		(
			( ( selected >= 0 ) && ( selected < static_cast<int> ( choices.size () ) ) )
				? choices [ static_cast<std::size_t> ( selected ) ].description
				: QString ()
		);
	}

	QString XmlImportDialog::notes_html ( const QStringList& notes )
	{
		if ( notes.isEmpty () )
		{
			return QString ();
		}

		QString items;

		for ( const QString& note : notes )
		{
			// Escaped, not interpolated raw: a note carries the XML parser's own message and the user's key names.

			items += QStringLiteral ( "<li>%1</li>" ).arg ( note.toHtmlEscaped () );
		}

		// The list is pulled back to the label's own left edge -- Qt's default <ul> margin would indent the whole
		// block away from the caption naming it, in a column that is already the narrower of the two.

		return QStringLiteral ( "<ul style=\"margin-left: 0px; -qt-list-indent: 1;\">%1</ul>" ).arg ( items );
	}

	void XmlImportDialog::apply_prose_tone ()
	{
		// BOTH blocks of explanatory prose (STYLE-15), which is why this is not the description's alone: the notes are
		// the same kind of text under the same kind of caption, and toning one of them by hand is how the two come to
		// disagree.
		//
		// A WIDGET palette, so it survives the application palette underneath it -- and reapplied on every palette or
		// style change, because the System theme can flip while this modal is open (ThemeService tracks the OS scheme)
		// and a colour computed once at construction would then be a distance from a surface that had moved.

		const QColor prose = dialog_dimmed_prose ( palette () );

		QLabel* const toned [ 2 ] = { descriptionLabel, notesLabel };

		for ( QLabel* const label : toned )
		{
			if ( label == nullptr )
			{
				continue;
			}

			QPalette labelPalette = label->palette ();

			labelPalette.setColor ( QPalette::WindowText, prose );

			label->setPalette ( labelPalette );
		}
	}

	void XmlImportDialog::update_text_key_enablement ()
	{
		const bool applies = controller->text_value_key_applies ();

		textValueKeyLabel->setEnabled ( applies );
		textValueKeyField->setEnabled ( applies );
	}
}

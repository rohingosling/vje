//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImportDialog -- the Import XML to JSON dialog (FILE-13, spec section 2.11): the picked file's name, the four
//   conversion strategies, the Infer scalar types toggle, Custom flattened's Text value key, and a live
//   syntax-highlighted preview of exactly what Import would produce.
//
//   IT DECIDES NOTHING, in the sense GoToDialog decides nothing. Which strategies exist, which is recommended, what the
//   preview shows, whether it had to be truncated, and which XML constructs the chosen strategy cannot fully represent
//   are all XmlImportController's -- this class turns four widgets' signals into four controller calls and paints
//   preview() into a CodeEditor. That is why the phase's behaviour is pinned in tst_xml_import_controller rather than
//   needing a modal loop no offscreen test can drive.
//
//   THE PREVIEW IS A READ-ONLY CodeEditor, not a second rendering surface. Section 2.11 asks for the Code View's
//   highlighting (EDITOR-07), and reusing the widget brings the gutter, the verified fixed-width font and the token
//   palette with it -- three things a bare QPlainTextEdit here would have had to restate. It is highlighted
//   unconditionally: the Code Editor group's syntax-highlighting switch is a preference about the surface the user
//   TYPES in, while this one exists to be judged at a glance, and honouring it here would put a settings store inside
//   an otherwise stateless dialog service.
//
//   RE-RENDERING IS IMMEDIATE FOR A CLICK AND COALESCED FOR TYPING. A strategy click or a toggle is one event, so the
//   preview follows it at once; the Text value key is one event per keystroke over a conversion of the whole file, so
//   it waits out config::xml_import::PREVIEW_TYPING_DELAY. That is the only place this dialog can feel slow (NFR-03).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace vje
{
	class CodeEditor;
	class JsonHighlighter;
	class XmlImportController;

	//*****************************************************************************************************************
	// Class: XmlImportDialog
	//*****************************************************************************************************************

	class XmlImportDialog : public QDialog
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// controller is injected and non-owning; it outlives the dialog (the import pipeline builds it, runs this, and
		// then reads the choices back out of it). fileName is display only.

		XmlImportDialog
		(
			XmlImportController* controller,
			const QString&       fileName,
			QWidget*             parent = nullptr
		);

		//=============================================================================================================
		// Value Accessors -- for the tests and for nothing else.
		//=============================================================================================================

	public:

		QListWidget* strategy_list      () const;
		QCheckBox*   infer_scalars_box  () const;
		QLineEdit*   text_value_key_field () const;
		CodeEditor*  preview_editor     () const;
		QLabel*      notes_label        () const;
		QPushButton* import_button      () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_strategy_changed ();
		void handle_infer_toggled    ( bool infer );
		void handle_text_key_edited  ();
		void refresh_preview         ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void build_strategy_list ();

		// The Text value key applies to Custom flattened alone. It stays VISIBLE and goes insensitive for the other
		// three, so a user who has never chosen that strategy can still see the option exists.

		void update_text_key_enablement ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		XmlImportController* controller;                       // Injected, non-owning.

		QListWidget*     strategyList   = nullptr;
		QCheckBox*       inferScalarsBox = nullptr;
		QLineEdit*       textValueKeyField = nullptr;
		QLabel*          textValueKeyLabel = nullptr;
		CodeEditor*      previewEditor  = nullptr;
		JsonHighlighter* highlighter    = nullptr;             // Owned by the preview editor's document.
		QLabel*          notesLabel     = nullptr;
		QPushButton*     importButton   = nullptr;             // Owned by the button box.
		QTimer*          typingTimer    = nullptr;
	};
}

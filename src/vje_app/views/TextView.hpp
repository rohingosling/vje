//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   TextView -- the read-only, notepad-style plain-text rendering of the selected node (EDITOR-06), so a formatted
//   representation can be selected and copied into another application.
//
//   IT IS A WINDOW ONTO vje_core, NOT A RENDERER OF ITS OWN. All eight table styles, the key-value listing, the Markdown forms and
//   their alignment already exist and are golden-matched byte-for-byte in TextViewRenderer (Phase 3). This class adds
//   exactly three things the core cannot have: WHICH node to render, a widget to show it in, and the SET-06 profile
//   read from settings.
//
//   WHICH NODE. EDITOR-06 defines the Text View as a rendering of "the Form View's PRESENTATION of the selected node",
//   so it resolves the selection through the same shared rule the Form View does (node_presentation.hpp) -- a scalar
//   renders the listing of its PARENT, not a lone value on a line. Switching between the two tabs therefore shows the
//   same node in two forms, which is the whole point of the pair.
//
//   THE FONT IS LOAD-BEARING. The renderer aligns separators and draws table rules by COUNTING CHARACTERS, so a
//   proportional font would leave every aligned column visibly ragged and every table border broken. The view forces the
//   platform's fixed-width font; this is a correctness constraint, not a style preference.
//
//   NO EDITING, and no re-render the user did not ask for: a re-render of the node already on screen preserves the
//   scroll position, so a selection that resolves back to the same container (arrowing between an object's scalars)
//   does not throw the reader back to the top of a long rendering.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/IEditorView.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QString>
#include <QWidget>

class QPlainTextEdit;

namespace vje
{
	class JsonDocument;
	class SettingsStore;

	//*****************************************************************************************************************
	// Class: TextView
	//*****************************************************************************************************************

	class TextView : public QWidget, public IEditorView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The settings store may be null, in which case the SET-06 defaults apply (aligned separators, ":", both
		// container rows included, Compact tables) -- the form the headless tests use.

		TextView ( JsonDocument* document, SettingsStore* settings, QWidget* parent = nullptr );

		//=============================================================================================================
		// IEditorView
		//=============================================================================================================

	public:

		QWidget* widget () override;

		void present ( const JsonPointer& pointer, SelectionOrigin origin ) override;

		void take_focus () override;

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QPlainTextEdit* text_edit () const;

		// The pointer this view is currently rendering -- the CONTAINER the presentation resolved to, not the
		// selection. A root pointer when nothing is rendered (check rendered_text().isEmpty() alongside it).

		const JsonPointer& rendered_pointer () const;

		QString rendered_text () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		// EDITOR-08: an edit committed in another view is visible here without reloading the document.

		void handle_document_changed ();

		// A SET-06 key changed. Filtered to the textView.* group, so an unrelated write (geometry, recent files) does
		// not re-render a large node.

		void handle_setting_changed ( const QString& key );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void render ( bool preserveScroll );

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*  document;
		SettingsStore* settings;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		QPlainTextEdit* textEdit = nullptr;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		JsonPointer currentContainer;   // What is rendered; the container, not the selection.
		bool        hasRendering = false;
	};

	//*****************************************************************************************************************
	// Class: TextViewProvider
	//*****************************************************************************************************************

	class TextViewProvider : public IEditorViewProvider
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		TextViewProvider ( JsonDocument* document, SettingsStore* settings );

		//=============================================================================================================
		// IEditorViewProvider
		//=============================================================================================================

	public:

		QString view_id       () const override;
		QString display_name  () const override;
		QString icon_name     () const override;
		int     display_order () const override;

		bool can_present ( const JsonNode* node ) const override;

		IEditorView* create_view ( QWidget* parent ) const override;

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static const QString VIEW_ID;

		static constexpr int DISPLAY_ORDER = 1;   // Between Form and Code (EDITOR-01).

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*  document;
		SettingsStore* settings;
	};
}

//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   SettingsDialog -- the modal master-detail Settings dialog of section 2.10 (SET-01): a group list on the left, the
//   selected group's settings on the right with labels and editors in aligned columns, OK applying everything at once
//   and Cancel discarding everything.
//
//   IT IS A RENDERER, NOT A FORM. It knows the six field KINDS and nothing about any individual setting: no key, no
//   default, no option list appears here. All of that is settings_schema's, which is what makes SET-02's "adding a group
//   is one new detail page plus one master-list entry" true -- and true without touching this file.
//
//   Every editor writes into the SettingsSnapshot, never into the store. That is the whole of Cancel: dropping the
//   snapshot. OK writes the snapshot in one pass, mirrors the theme through ThemeService (the one setting that also has
//   to REPAINT something, section 2.9), and returns.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "dialogs/settings_schema.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QListWidget;
class QStackedWidget;
class QWidget;

namespace vje
{
	class IDialogService;
	class SettingsStore;
	class ThemeService;

	//*****************************************************************************************************************
	// Class: SettingsDialog
	//*****************************************************************************************************************

	class SettingsDialog : public QDialog
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// groups is the schema to render (settings_schema_with_toolbar). dialogs supplies the SET-09 folder picker, so
		// even the Browse button goes through the one modal seam.

		SettingsDialog
		(
			std::vector<SettingsGroup> groups,
			SettingsStore*             settings,
			ThemeService*              theme,
			IDialogService*            dialogs,
			QWidget*                   parent = nullptr
		);

		//=============================================================================================================
		// Construction Helpers
		//=============================================================================================================

	private:

		void build_layout ();

		// The detail page for one group: a form of label / editor rows, each editor bound to the snapshot.

		QWidget* build_page ( const SettingsGroup& group );

		QWidget* build_editor ( const SettingsField& field );

		// Rebuild every detail page from the CURRENT snapshot, preserving which group is open.
		//
		// Reset re-renders rather than writing each editor back, deliberately. Pushing values into live widgets would be
		// a second statement of the field-kind-to-widget binding build_editor already owns -- seven kinds, each needing
		// its own setter and its own signal suppression so the write-back did not fight the snapshot it came from. The
		// binding is stated once; a reset just runs it again.

		void rebuild_pages ();

		// Re-apply every field's enabledByKey dependency to the live editors (SET-09's folder and file name).

		void refresh_field_dependencies ();

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_accepted ();                                   // OK: apply the snapshot as one operation.

		// Restore Defaults: confirm, then refill the SNAPSHOT with the schema's defaults and re-render.
		//
		// Writes nothing to the store, so it obeys the same all-or-nothing contract as every editor -- OK commits the
		// reset, Cancel discards it (SET-01). The confirmation is still warranted: the reset spans every group, so it
		// discards edits the user may have made on pages they cannot currently see.
		//
		// Scope is the schema, which is exactly what the dialog shows. Window geometry, splitter sizes, recent files and
		// the last XML import strategy are persisted UI state with no dialog surface, and a Settings button that forgot
		// the window layout would be a surprise rather than a reset.

		void handle_restore_defaults ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		SettingsStore*  settings;                                  // Non-owning.
		ThemeService*   theme;
		IDialogService* dialogs;

		std::vector<SettingsGroup> groups;
		SettingsSnapshot           snapshot;

		QListWidget*    groupList  = nullptr;
		QStackedWidget* detailPages = nullptr;

		// The live editor for each field key, so a dependency change can enable or disable it without rebuilding a page.

		QHash<QString, QWidget*> editorsByKey;
	};
}

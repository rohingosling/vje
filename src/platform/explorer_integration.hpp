//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   explorer_integration -- VJE's entries in Windows Explorer's menus for .json files (FILE-15, SET-15, CLI-08), and
//   the platform layer's first real divergence: Windows writes them, and every other platform reports the feature
//   unsupported (architecture.md section 12).
//
//   TWO ENTRIES, ONE PER MENU, because the two menus are fed differently (measured on Windows 11, 2026-09-26):
//
//     Open with > VJE   Windows 11's default menu builds Open with from the ProgIDs listed under the extension's
//                       OpenWithProgids. So VJE registers a ProgID of its own, VJE.JsonFile, and lists it there. The
//                       name Open with shows is the FriendlyAppName on the ProgID's shell\open key: without it the entry
//                       read "vje.exe", and with it "VJE" -- even while the shell's MuiCache still held "vje.exe" for
//                       the same executable.
//
//     Edit in VJE       A static verb under SystemFileAssociations\.json\shell. The modern menu shows no static verb at
//                       all, which is exactly what puts this one behind Show more options and nowhere else. Registered
//                       there rather than under whichever ProgID owns .json, so VJE never edits another application's
//                       registration -- and the per-user SystemFileAssociations key IS honoured: the classic menu of a
//                       .json file listed "Edit in VJE" the moment it was written, and a .txt file's did not.
//
//   EVERYTHING IS PER USER, under HKEY_CURRENT_USER -- which is what lets a setting, rather than an installer running as
//   administrator, switch it on and off. And VJE NEVER MAKES ITSELF THE DEFAULT: it writes neither .json's default
//   value nor the hash-guarded UserChoice (UserChoiceLatest on current Windows 11) that records the user's choice.
//
//   THE PLAN IS DATA. registration_plan lists every value the registration writes, and it is pure and compiled on every
//   platform, so a headless test reads exactly what the Windows implementation applies. Removal is DERIVED from the
//   same list rather than stated beside it: delete each value the plan names, then delete each key on the plan's paths
//   that is left EMPTY, deepest first. That is what makes the removal set the write set by construction -- a key some
//   other application put a value or a subkey into is not empty, and survives.
//
//   THE CLASSES ROOT IS A PARAMETER so the tests apply the plan to a scratch root -- under the key
//   HKEY_CURRENT_USER\Software\VJE-Test -- instead of the live one. Only the live root tells Explorer that associations
//   changed.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

#include <vector>

namespace vje::platform::explorer_integration
{
	//-----------------------------------------------------------------------------------------------------------------
	// Names.
	//-----------------------------------------------------------------------------------------------------------------

	inline const QString EXTENSION         = QStringLiteral ( ".json" );
	inline const QString PROG_ID           = QStringLiteral ( "VJE.JsonFile" );        // Open with > VJE.
	inline const QString EDIT_VERB         = QStringLiteral ( "VJE.Edit" );            // Edit in VJE; never a bare "edit".
	inline const QString LIVE_CLASSES_ROOT = QStringLiteral ( "Software\\Classes" );   // Under HKEY_CURRENT_USER.

	//-----------------------------------------------------------------------------------------------------------------
	// One registry value the registration writes: a key relative to the classes root, the value's name (empty for the
	// key's default value), and its data. Every value is a string (REG_SZ).
	//-----------------------------------------------------------------------------------------------------------------

	struct RegistryValue
	{
		QString key;
		QString name;
		QString data;
	};

	// Every value registering `windowProgramPath` writes, in write order. The keys and names do not depend on the path
	// -- only the data does -- which is what lets removal work without knowing which copy of VJE registered.

	std::vector<RegistryValue> registration_plan ( const QString& windowProgramPath );

	// The keys removal considers once the plan's values are gone: every key on the plan's paths, each exactly once,
	// DEEPEST FIRST so a parent is examined after its children. Relative to the classes root, which is never among them.

	std::vector<QString> removal_order ( const std::vector<RegistryValue>& plan );

	//-----------------------------------------------------------------------------------------------------------------
	// The operations. On a platform that does not support the feature they do nothing and say so.
	//-----------------------------------------------------------------------------------------------------------------

	struct Outcome
	{
		bool    succeeded = true;
		QString problem;                                   // One sentence when it failed; empty otherwise.
	};

	// Whether this platform offers the feature at all. The settings schema and the command registry both ask this, so
	// the System group has no such row and the command line no such commands where it answers false (SET-15, CLI-01).

	bool is_supported ();

	// Write the plan for `windowProgramPath`. Writing it twice changes nothing. A failure part-way removes whatever was
	// written, so a failed registration leaves no half of one behind.

	Outcome register_for ( const QString& windowProgramPath, const QString& classesRoot = LIVE_CLASSES_ROOT );

	// Remove exactly what the plan writes, and any key that leaves empty. Succeeds when there is nothing to remove --
	// the case an uninstaller meets.

	Outcome unregister ( const QString& classesRoot = LIVE_CLASSES_ROOT );

	// Whether ANY of the plan's values is present -- something for unregister to remove.

	bool is_registered ( const QString& classesRoot = LIVE_CLASSES_ROOT );

	// Whether EVERY value of the plan for `windowProgramPath` is present with exactly the data it would write -- the
	// launch-time question "do the entries still point at this copy of VJE?".

	bool is_registered_for ( const QString& windowProgramPath, const QString& classesRoot = LIVE_CLASSES_ROOT );
}

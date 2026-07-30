//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ClipboardService -- the QClipboard boundary for node and cell cut / copy / paste (EDIT-06, EDITOR-11). It carries
//   DUAL-FORMAT content:
//
//     - a private "application/x-vje-json" MIME type holding the value's EXACT JSON text, so an in-app paste keeps the
//       value's type and its raw number tokens (FILE-10) -- a plain-text "1.50" is indistinguishable from a string,
//       where the private form is unambiguously a number;
//     - plain text for external targets. For a NODE that is the subtree's JSON text (EDIT-06); for a CELL it is the
//       tree-copy form (a string unquoted, a number as its raw token, true / false, a container as its JSON text).
//
//   A node copy additionally carries the source member's KEY in "application/x-vje-json-key" when the node is an object
//   member, so a paste into an object can reuse it (de-duplicated) rather than inventing one -- copy "email", paste it
//   into another object, and it lands as "email".
//
//   TESTABILITY. The encode / decode is a set of PURE static helpers over QMimeData (Qt Core only, no live clipboard),
//   so the format is pinned by a headless test; only the copy / paste methods touch the injected QClipboard.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QClipboard;
class QMimeData;

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// The private MIME types. Named once so the writer and the reader cannot disagree about the string.
	//-----------------------------------------------------------------------------------------------------------------

	namespace clipboard_mime
	{
		inline const QString VJE_JSON     = QStringLiteral ( "application/x-vje-json" );        // The value's exact JSON.
		inline const QString VJE_JSON_KEY = QStringLiteral ( "application/x-vje-json-key" );    // A node copy's source key.
	}

	//*****************************************************************************************************************
	// Class: ClipboardService
	//*****************************************************************************************************************

	class ClipboardService : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The clipboard is injected (the app passes QGuiApplication::clipboard(); a test passes the offscreen one), so
		// the service carries no global dependency of its own.

		explicit ClipboardService ( QClipboard* clipboard, QObject* parent = nullptr );

		//=============================================================================================================
		// Copy
		//=============================================================================================================

	public:

		// Copy a whole node / subtree (EDIT-06). sourceKey is the node's member key when it is an object member (carried
		// so a paste into an object can reuse it), else empty.

		void copy_node ( const JsonNode& node, const QString& sourceKey = QString () );

		// Copy a single array-table cell value (EDITOR-11).

		void copy_cell ( const JsonNode& node );

		// Put PLAIN TEXT on the clipboard and nothing else -- Copy JSON Pointer (FIND-05).
		//
		// Deliberately not one of the copy_* pair above, and the difference is the point: it must NOT set the private
		// application/x-vje-json format, or the very next Ctrl+V would paste a NODE instead of the pointer text the user
		// asked for. It still goes through this service rather than touching QClipboard at the call site, because this
		// class is the application's single QClipboard boundary (architecture section 4.5).
		//
		// An EMPTY string is a legitimate argument: the root's JSON Pointer is the empty string (RFC 6901), so copying
		// the root's pointer genuinely puts nothing on the clipboard. The caller is the one that has to say so.

		void set_plain_text ( const QString& text );

		//=============================================================================================================
		// Paste
		//=============================================================================================================

	public:

		// The clipboard's value as a node -- the private exact JSON first, else the plain text parsed as JSON, else a
		// string; nullptr when the clipboard holds nothing to paste. Used by both node paste and cell paste.

		std::unique_ptr<JsonNode> value () const;

		// The source member key carried by a node copy, or empty. Meaningful only while the private format is present.

		QString source_key () const;

		// Is there anything to paste? True when the clipboard carries the private format or any non-empty text.
		//
		// Answered from a CACHED flag refreshed on QClipboard::dataChanged -- one OS clipboard read per change, not
		// one per query. The enablement recompute asks this on every keyboard-focus change, and QClipboard::mimeData
		// on Windows is a synchronous cross-process call that can stall behind whichever application owns the
		// clipboard (NFR-03). The cache is only as fresh as dataChanged is reliable, which Qt guarantees on the
		// supported platforms; a missed external change corrects itself on the next one.

		bool has_content () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The system clipboard's contents changed (forwarded from QClipboard::dataChanged), so Paste enablement can be
		// recomputed (disabled-not-hidden).

		void content_changed ();

		//=============================================================================================================
		// Pure encode / decode (Qt Core only -- pinned by a headless test).
		//=============================================================================================================

	public:

		// The EDITOR-11 plain-text form of a cell value.

		static QString cell_plain_text ( const JsonNode& node );

		// Populate a QMimeData for a node copy / a cell copy. Ownership stays with the caller.

		static void fill_node_mime ( QMimeData* data, const JsonNode& node, const QString& sourceKey );
		static void fill_cell_mime ( QMimeData* data, const JsonNode& node );

		// The decode side of value() / source_key() / has_content(), over an arbitrary QMimeData.

		static std::unique_ptr<JsonNode> value_from_mime      ( const QMimeData* data );
		static QString                   source_key_from_mime ( const QMimeData* data );
		static bool                      mime_has_content     ( const QMimeData* data );

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		// QClipboard::dataChanged: refresh the has_content cache (the one OS read per change), then forward as
		// content_changed so enablement recomputes against the fresh answer.

		void handle_clipboard_changed ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QClipboard* clipboard;                                     // Non-owning.

		bool contentPresent = false;                               // The has_content() cache (see above).
	};
}

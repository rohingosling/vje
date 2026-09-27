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
//   A MULTI-NODE copy (EDIT-14) uses a THIRD private type, "application/x-vje-json-list", carrying the subtrees in
//   document order each with its own key. It is a separate type rather than a repetition of the single one because
//   the two have to be distinguishable by a reader: the natural single-format encoding of several nodes is a JSON
//   ARRAY, which is indistinguishable from one node that happens to be an array -- so pasting a copied array would
//   scatter its elements. A copy of ONE node writes the single format, so nothing about the pre-EDIT-14 clipboard
//   changes for the case that already worked.
//
//   THE NAMING IS THE PARTITION, and it is a rule about this API rather than a remark about one method:
//
//     copy_*  writes the DUAL format -- the private exact JSON, plus plain text for external targets.
//     set_*   writes PLAIN TEXT ALONE, and must never write a private format.
//
//   WHY THE SECOND HALF MATTERS. Some commands copy a NAME rather than a value -- a JSON Pointer (FIND-05), an object
//   member's key (EDITOR-14), a query's result list (QUERY-08). What the user asked for there is text, and if the
//   private format went with it the very next Ctrl+V would paste a NODE into their document instead. That is a wrong
//   edit to the user's data from a gesture that looked like copying a word: undoable, but only once noticed.
//
//   So a command that copies a name calls set_plain_text, and the reason lives HERE rather than at each call site --
//   a rule restated at every caller is a rule that drifts, and counting the callers in prose ages badly the moment a
//   new one lands. tst_clipboard_service::set_plain_text_leaves_no_private_format pins it once, for every caller
//   present and future.
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

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

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
		inline const QString VJE_JSON      = QStringLiteral ( "application/x-vje-json" );       // The value's exact JSON.
		inline const QString VJE_JSON_KEY  = QStringLiteral ( "application/x-vje-json-key" );   // A node copy's source key.
		inline const QString VJE_JSON_LIST = QStringLiteral ( "application/x-vje-json-list" );  // A multi-node copy (EDIT-14).

		// A table ROW or COLUMN copy (EDITOR-18). A fourth type rather than a reuse of the list, for the reason the
		// list is not a reuse of the single: what distinguishes a row from a column is not the values but which way
		// they run, and a paste has to refuse the wrong one by NAME rather than by failing to recognize it.

		inline const QString VJE_JSON_TABLE = QStringLiteral ( "application/x-vje-json-table" );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// One entry of a multi-node copy (EDIT-14): a subtree and the key it was a member under, empty for an array
	// element. The two travel together rather than as parallel lists because a list of keys that could disagree in
	// length with a list of values is a defect waiting for a ragged selection.
	//
	// Two types rather than one, because the two directions own their subtree differently: a COPY borrows nodes that
	// live in the document, and a PASTE hands back nodes the clipboard just minted.
	//-----------------------------------------------------------------------------------------------------------------

	struct ClipboardNodeRef
	{
		const JsonNode* node = nullptr;
		QString         key;
	};

	struct ClipboardNodeValue
	{
		std::unique_ptr<JsonNode> node;
		QString                   key;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A table row or column copy (EDITOR-18).
	//
	// The shape travels WITH the values rather than being implied by a format per shape, because a paste of the wrong
	// one has to say what is on the clipboard -- "a column cannot be pasted onto a row" -- and a reader that merely
	// failed to find its own format could only say that nothing recognizable was there.
	//
	// A cell the element LACKS travels as a null pointer, which is the same "absent" convention json_order and
	// cell_paste_plan already use, and is deliberately not the same as a cell holding null: EDITOR-18 leaves an absent
	// source cell's target untouched, where a null one overwrites it.
	//-----------------------------------------------------------------------------------------------------------------

	enum class TableSelectionShape
	{
		Row,
		Column
	};

	// Each cell travels with the MEMBER KEY of the column it came from, which is what lets a paste GROW its target:
	// appending an element, or adding a member, needs a name for the value and only the source has one. A null key
	// means the source column had none -- a single-value (scalar) table, whose cells are the elements themselves.
	//
	// Two types rather than one, for ClipboardNodeRef / ClipboardNodeValue's reason: a copy borrows nodes that live in
	// the document, and a paste hands back nodes the clipboard just minted.

	struct TableSelectionRef
	{
		const JsonNode* value = nullptr;                                       // nullptr is an ABSENT cell.
		QString         key;                                                   // Null when the source has no key.
	};

	struct TableSelectionCell
	{
		std::unique_ptr<JsonNode> value;                                       // Null is an ABSENT cell.
		QString                   key;
	};

	struct TableSelectionValue
	{
		TableSelectionShape             shape = TableSelectionShape::Row;
		std::vector<TableSelectionCell> cells;

		// Were the cells MEMBERS of object elements (true), or was the source a single-value table whose one cell per
		// row IS the element (false)? Every cell carries a name either way -- a single-value column is named after its
		// array (section 2.12) -- so the names alone cannot say, and a row INSERTED into an array of values needs to:
		// a value row goes in bare, an object row as an object (EDITOR-18, 2026-09-23).

		bool keyed = true;

		bool is_empty () const { return cells.empty (); }
	};

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

		// Copy SEVERAL nodes as one ordered list (EDIT-14). The caller passes them in document order and that order is
		// what a paste replays.
		//
		// A LIST OF ONE IS WRITTEN AS A SINGLE-NODE COPY, deliberately, and it is the reason nothing about the Phase 9
		// clipboard changes for the case that already worked: a one-node selection copied here is byte-identical to
		// copy_node, so an external paste, an in-app paste and a cell paste all behave exactly as before. The list
		// format exists for the case the single format cannot express, and for no other.

		void copy_nodes ( const QList<ClipboardNodeRef>& nodes );

		// Copy a single array-table cell value (EDITOR-11).

		void copy_cell ( const JsonNode& node );

		// Put PLAIN TEXT on the clipboard and nothing else -- the set_* half of the naming rule at the head of this
		// file, which is where the reason lives. It goes through this service rather than touching QClipboard at the
		// call site because this class is the application's single QClipboard boundary (architecture section 4.5).
		//
		// An EMPTY string is a legitimate argument: the root's JSON Pointer is the empty string (RFC 6901), so copying
		// the root's pointer genuinely puts nothing on the clipboard. The caller is the one that has to say so -- and
		// this must still CLEAR what was there, so an empty copy following a node copy does not leave that node behind
		// for the next paste to find.

		void set_plain_text ( const QString& text );

		// EDITOR-18: a table row or column. cells carries the values in order, a null entry meaning the element LACKS
		// that member; plainText is what an external target receives and differs by shape (see the .cpp).

		void copy_table_selection
		(
			TableSelectionShape                    shape,
			const std::vector<TableSelectionRef>&  cells,
			const QString&                         plainText,
			bool                                   keyed = true
		);

		//=============================================================================================================
		// Paste
		//=============================================================================================================

	public:

		// The clipboard's value as a node -- the private exact JSON first, else the plain text parsed as JSON, else a
		// string; nullptr when the clipboard holds nothing to paste. Used by both node paste and cell paste.

		std::unique_ptr<JsonNode> value () const;

		// The clipboard's multi-node list (EDIT-14), in the order it was copied; EMPTY when the clipboard does not
		// carry one -- which is every single-node copy, every cell copy and everything that came from outside VJE.
		//
		// A caller therefore asks this FIRST and falls back to value(): "is this a list?" is a question only the
		// private list format can answer yes to, so there is no ambiguity to resolve and no ordering rule to remember
		// beyond that one.

		std::vector<ClipboardNodeValue> value_list () const;

		// EDITOR-18. Empty cells means there is no table selection on the clipboard -- only the private table format
		// answers here, never plain text, for value_list's own reason: an external editor's text is one value, and
		// reading it as a row or a column would turn one paste into a different edit from a gesture that looked the same.

		TableSelectionValue table_selection () const;

		// The source member key carried by a node copy, or empty. Meaningful only while the private format is present.

		QString source_key () const;

		// The clipboard's PLAIN TEXT, unparsed. EDITOR-14's key paste needs the text as typed rather than as a value:
		// a member key is a literal string, so " 42 " names a key spelled with spaces and is not the number 42. Asking
		// value() there would parse it into a node and lose exactly that.

		QString plain_text () const;

		// EDITOR-24: the plain text ONLY when it came from another application -- empty whenever one of VJE's own
		// formats is present. Every VJE copy writes plain text for external targets alongside its private format, and
		// that text is a RENDERING (a copied column's values one per line, a string value with its line breaks), so
		// reading it as a spreadsheet's block would turn our own copy into a different paste from the one it was.

		QString external_text () const;

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

		// Populate a QMimeData for a multi-node copy (EDIT-14). A list of one delegates to fill_node_mime, so the
		// single-node format is the one thing that can come out of a one-node copy.

		static void fill_node_list_mime ( QMimeData* data, const QList<ClipboardNodeRef>& nodes );

		// EDIT-14's plain text: what an EXTERNAL editor receives from a multi-node copy. Each node is written as it
		// appears in its parent -- an object member as "key": value, an array element as the bare value -- joined by
		// commas, one per line, so the result drops straight between an object's or an array's braces. The single-node
		// form stays the bare subtree (EDIT-06), which is what a one-node copy still writes.

		static QString node_list_plain_text ( const QList<ClipboardNodeRef>& nodes );

		// The decode side of value() / value_list() / source_key() / has_content(), over an arbitrary QMimeData.

		static void fill_table_mime
		(
			QMimeData*                             data,
			TableSelectionShape                    shape,
			const std::vector<TableSelectionRef>&  cells,
			const QString&                         plainText,
			bool                                   keyed = true
		);

		static TableSelectionValue table_selection_from_mime ( const QMimeData* data );

		static std::unique_ptr<JsonNode>       value_from_mime      ( const QMimeData* data );
		static std::vector<ClipboardNodeValue> value_list_from_mime ( const QMimeData* data );
		static QString                         source_key_from_mime ( const QMimeData* data );
		static bool                            mime_has_content     ( const QMimeData* data );
		static QString                         external_text_from_mime ( const QMimeData* data );

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

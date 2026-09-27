//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImportController -- what the Import XML to JSON dialog MEANS (FILE-13, spec section 2.11), with no widget
//   anywhere near it.
//
//   THE SPLIT IS THE SAME ONE PHASE 11 MADE FOR FIND. What the dialog CONTAINS is a list, a check box, a text field and
//   a preview pane; what the dialog DECIDES is which strategies exist and in what order, which one is preselected, what
//   the preview shows for a given choice, when it is truncated, which XML constructs the chosen strategy cannot fully
//   represent, and what an Import hands back. All of the second list is here, so it is pinned headlessly and
//   XmlImportDialog is left rendering preview() and reporting the user's clicks.
//
//   THE XML IS PARSED ONCE. Section 2.11 asks for a preview that "updates in real time" and "feels instantaneous for
//   typical files", and the two halves of an import cost very different amounts: parsing is proportional to the file and
//   depends on nothing the dialog can change, while converting is a walk over an already-built tree. So the tree is
//   built in the constructor and every strategy change re-converts it. A file that does not parse has no tree, cannot be
//   imported under any strategy, and says so in the preview rather than through an error box -- the GoToDialog rule:
//   report in place, because the user's next move is to look at the message and decide, not to dismiss a box first.
//
//   THE PREVIEW IS THE IMPORT. It is produced by the same XmlImporter::convert_tree and the same JsonFormatter that
//   File > Save would use (SET-07), through the profile handed in -- so "what you previewed is what you imported" holds
//   by construction rather than by two code paths agreeing. Truncation is the one difference, and it is announced.
//
//   REGENERATION IS LAZY, for the reason FindController's match list is: a dialog that has not been asked anything since
//   the last change has nothing to show, and a caller that sets three options in a row should pay for one conversion.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "controllers/converters.hpp"

#include <vje_core/convert/XmlImporter.hpp>
#include <vje_core/services/JsonFormatter.hpp>

#include <QString>
#include <QStringList>

#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One row of the strategy selector (section 2.11). The name and the description come from the strategy itself, so
	// the list cannot describe a strategy differently from the way the strategy describes itself; "(Recommended)" is
	// appended to whichever one answers is_default(), rather than being written against a name here.
	//-----------------------------------------------------------------------------------------------------------------

	struct XmlStrategyChoice
	{
		XmlImportStrategyKind kind        = XmlImportStrategyKind::DirectAttributeKeys;
		QString               displayName;                     // Without the marker.
		QString               description;                     // The one-line summary shown beneath the name.
		bool                  recommended = false;             // The default, marked "(Recommended)".

		// The name as the list shows it: the display name, plus the marker when this is the recommended one.

		QString labelled_name () const;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// What the preview area shows. Three separable things, because they are shown in three places and any of them can
	// be present without the others: the JSON itself, the reason there is none, and the notes about what the conversion
	// could not carry across.
	//-----------------------------------------------------------------------------------------------------------------

	struct XmlImportPreview
	{
		QString text;                                          // The formatted JSON, cut at the line limit.
		bool    ok = false;                                    // False only when the XML did not parse.
		QString error;                                         // The parse failure, with line and column (FILE-06).

		// Constructs the chosen strategy cannot fully represent, one sentence each, in a fixed order so a preview does
		// not reshuffle its own notes as the user clicks between strategies.

		QStringList warnings;

		// Empty unless the rendering was cut; it names both counts, because "truncated" without a size tells the user
		// nothing about whether what they are looking at is most of the file or a thousandth of it.

		QString truncationNote;
	};

	//*****************************************************************************************************************
	// Class: XmlImportController
	//*****************************************************************************************************************

	class XmlImportController
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// xmlText is the file's whole contents; initialOptions is the persisted last choice, which decides what is
		// preselected (SET-08); format is the document format profile the preview renders through (SET-07).

		XmlImportController
		(
			const QString&       xmlText,
			const ImportOptions& initialOptions,
			const FormatProfile& format
		);

		//=============================================================================================================
		// The strategy list -- fixed, and the same for every instance.
		//=============================================================================================================

	public:

		// The four strategies in the order section 2.11 tabulates them, which is the order XmlImportStrategyKind
		// declares them. Built once from the strategies themselves.

		static const std::vector<XmlStrategyChoice>& strategy_choices ();

		// Where a kind sits in that list. Never negative: every kind is in the list, and the list is the enumeration.

		static int index_of_strategy ( XmlImportStrategyKind kind );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		int                   selected_index    () const;      // Into strategy_choices().
		XmlImportStrategyKind strategy          () const;
		bool                  infer_scalar_types () const;
		const QString&        text_value_key    () const;      // Empty means the element's own name.

		// Does the Text value key apply to the current strategy? Custom flattened alone -- the field stays visible and
		// goes insensitive for the other three, so its existence is discoverable rather than appearing and vanishing.

		bool text_value_key_applies () const;

		// The chosen options, ready to hand to the import pipeline and to persist (SET-08). The text value key is
		// carried whatever the strategy, so switching to Custom flattened and back does not silently forget it.

		ImportOptions options () const;

		// May the user press Import? Only when the XML parsed: no strategy can convert a file that is not XML, and an
		// enabled button whose only outcome is an error box is a worse answer than a disabled one beside the reason.

		bool can_import () const;

		//=============================================================================================================
		// Mutators -- each marks the preview stale rather than regenerating it.
		//=============================================================================================================

	public:

		void select_index          ( int index );              // Out of range is ignored.
		void set_strategy          ( XmlImportStrategyKind kind );
		void set_infer_scalar_types ( bool infer );
		void set_text_value_key    ( const QString& key );

		//=============================================================================================================
		// The preview
		//=============================================================================================================

	public:

		// Regenerated on the first call after any change, and cached until the next one.

		const XmlImportPreview& preview () const;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void regenerate () const;

		// The notes for the current strategy over the parsed tree and the converted result. Counts rather than a bare
		// "some", because the user's question is how much of their file this affects.

		QStringList collect_warnings ( const JsonNode& converted ) const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		XmlImporter::ParseResult parsed;                       // Built once in the constructor.
		FormatProfile            format;                       // SET-07: the preview renders as a save would write.

		XmlImportStrategyKind chosenStrategy   = XmlImportStrategyKind::DirectAttributeKeys;
		bool                  inferScalarTypes = false;
		QString               textValueKey;

		// The cache and its staleness flag. Mutable because preview() is the const question the dialog asks, and
		// whether the answer had to be recomputed is not part of what the caller sees (FindController's rule).

		mutable XmlImportPreview cachedPreview;
		mutable bool             previewStale = true;
	};
}

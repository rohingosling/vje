//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for XmlImportController -- the Import XML to JSON dialog's whole decision surface (FILE-13, spec section
//   2.11), asserted with no dialog anywhere in the file. That is the phase's design in one suite: what the dialog
//   CONTAINS needs a screen, what it MEANS does not.
//
//   What is pinned:
//
//     - The strategy list: all four, in the order section 2.11 tabulates them, each carrying the strategy's own name
//       and description, with "(Recommended)" on the default and on nothing else.
//     - Preselection from the persisted options (SET-08), including the tolerant fall-back to the recommended one.
//     - Preview regeneration: the JSON changes when the strategy changes, when Infer scalar types changes, and when
//       Custom flattened's text value key changes -- and it is the IMPORT's own output, not a second rendering.
//     - The error path: unparseable XML yields no preview, a reason with a position, and a refused Import.
//     - Import-returns-the-choice versus Cancel-returns-nothing, as the options() a caller reads back.
//     - The degraded-construct notes (namespaces, mixed content, the text-key collision, duplicate keys) and the
//       truncation note.
//
//   Headless: the controller is Qt Core plus vje_core, so this is a plain test target.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/XmlImportController.hpp"

#include "AppConfig.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

#include <QString>
#include <QStringList>

using namespace vje;

namespace
{
	// The spec's own worked example (section 2.11), so the strategy assertions below are the documented output rather
	// than a shape invented for the test.

	const QString SENTENCE_XML = QStringLiteral
	(
		"<sentence name=\"test-sentence\" description=\"Short example sentence.\" tag=\"0\">"
		"The cat went up the hill.</sentence>"
	);

	FormatProfile default_profile ()
	{
		return FormatProfile ();                               // 2 spaces, Allman, separators not aligned.
	}

	ImportOptions default_options ()
	{
		return ImportOptions ();                               // Direct attribute keys, inference off, no text key.
	}
}

class TestXmlImportController : public QObject
{
	Q_OBJECT

private slots:

	void the_four_strategies_are_listed_in_spec_order_with_one_recommendation ();
	void the_persisted_strategy_is_preselected ();
	void the_preview_is_the_conversion_the_import_would_perform ();
	void changing_the_strategy_regenerates_the_preview ();
	void inferring_scalar_types_regenerates_the_preview ();
	void the_text_value_key_applies_to_custom_flattened_alone ();
	void unparseable_xml_reports_a_position_and_refuses_the_import ();
	void the_options_read_back_are_the_ones_that_were_set ();
	void namespaces_and_mixed_content_are_reported_as_notes ();
	void a_text_key_collision_is_reported_and_the_key_is_suffixed ();
	void duplicate_keys_are_reported_for_the_strategy_that_causes_them ();
	void a_large_rendering_is_truncated_with_a_note ();
	void a_clean_document_produces_no_notes_at_all ();
};

//---------------------------------------------------------------------------------------------------------------------
// The strategy list
//---------------------------------------------------------------------------------------------------------------------

void TestXmlImportController::the_four_strategies_are_listed_in_spec_order_with_one_recommendation ()
{
	const std::vector<XmlStrategyChoice>& choices = XmlImportController::strategy_choices ();

	QCOMPARE ( static_cast<int> ( choices.size () ), 4 );

	QCOMPARE ( static_cast<int> ( choices [ 0 ].kind ), static_cast<int> ( XmlImportStrategyKind::BadgerFish ) );
	QCOMPARE ( static_cast<int> ( choices [ 1 ].kind ), static_cast<int> ( XmlImportStrategyKind::DirectAttributeKeys ) );
	QCOMPARE ( static_cast<int> ( choices [ 2 ].kind ), static_cast<int> ( XmlImportStrategyKind::GroupedAttributes ) );
	QCOMPARE ( static_cast<int> ( choices [ 3 ].kind ), static_cast<int> ( XmlImportStrategyKind::CustomFlattened ) );

	// Exactly one recommendation, and it is Direct attribute keys (section 2.11).

	int recommendations = 0;

	for ( const XmlStrategyChoice& choice : choices )
	{
		QVERIFY ( !choice.displayName.isEmpty () );
		QVERIFY ( !choice.description.isEmpty () );

		if ( choice.recommended )
		{
			++recommendations;
		}

		// The marker is on the labelled name and never on the bare display name, so a caller showing one of them does
		// not accidentally show the other's text.

		QVERIFY ( !choice.displayName.contains ( QStringLiteral ( "Recommended" ) ) );
		QCOMPARE ( choice.labelled_name ().contains ( QStringLiteral ( "(Recommended)" ) ), choice.recommended );
	}

	QCOMPARE ( recommendations, 1 );
	QVERIFY  ( choices [ 1 ].recommended );

	QCOMPARE ( XmlImportController::index_of_strategy ( XmlImportStrategyKind::CustomFlattened ), 3 );
}

void TestXmlImportController::the_persisted_strategy_is_preselected ()
{
	ImportOptions stored = default_options ();

	stored.xmlStrategy         = XmlImportStrategyKind::GroupedAttributes;
	stored.xmlInferScalarTypes = true;
	stored.xmlTextValueKey     = QStringLiteral ( "value" );

	const XmlImportController controller ( SENTENCE_XML, stored, default_profile () );

	QCOMPARE ( controller.selected_index (),
	           XmlImportController::index_of_strategy ( XmlImportStrategyKind::GroupedAttributes ) );

	QCOMPARE ( controller.infer_scalar_types (), true );
	QCOMPARE ( controller.text_value_key (), QStringLiteral ( "value" ) );

	// With nothing stored the preselection is the recommended strategy, because that is what an absent setting reads
	// back as -- the dialog does not restate the default (SET-08).

	const XmlImportController fresh ( SENTENCE_XML, default_options (), default_profile () );

	QVERIFY ( XmlImportController::strategy_choices () [ static_cast<std::size_t> ( fresh.selected_index () ) ].recommended );
}

//---------------------------------------------------------------------------------------------------------------------
// The preview
//---------------------------------------------------------------------------------------------------------------------

void TestXmlImportController::the_preview_is_the_conversion_the_import_would_perform ()
{
	const XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	const XmlImportPreview& preview = controller.preview ();

	QVERIFY ( preview.ok );
	QVERIFY ( preview.error.isEmpty () );

	// Byte-for-byte the import's own output through the save profile (SET-07 / FILE-03): the same convert_tree and the
	// same JsonFormatter, so a preview that agreed with the import by coincidence is not possible.

	const std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy
	(
		XmlImportStrategyKind::DirectAttributeKeys
	);

	const XmlImporter::Result imported = XmlImporter::import_text ( SENTENCE_XML, *strategy, false );

	QVERIFY ( imported.ok );
	QCOMPARE ( preview.text, JsonFormatter::format ( *imported.root, default_profile () ) );

	// And it is the documented mapping: an attribute became a sibling member, the text landed under "content".

	QVERIFY ( preview.text.contains ( QStringLiteral ( "\"name\": \"test-sentence\"" ) ) );
	QVERIFY ( preview.text.contains ( QStringLiteral ( "\"content\": \"The cat went up the hill.\"" ) ) );
}

void TestXmlImportController::changing_the_strategy_regenerates_the_preview ()
{
	XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	const QString direct = controller.preview ().text;

	QVERIFY ( direct.contains ( QStringLiteral ( "\"name\"" ) ) );
	QVERIFY ( !direct.contains ( QStringLiteral ( "\"@name\"" ) ) );

	controller.set_strategy ( XmlImportStrategyKind::BadgerFish );

	const QString badgerFish = controller.preview ().text;

	QVERIFY ( badgerFish != direct );
	QVERIFY ( badgerFish.contains ( QStringLiteral ( "\"@name\"" ) ) );
	QVERIFY ( badgerFish.contains ( QStringLiteral ( "\"$\"" ) ) );

	// select_index is the list's way in, and out-of-range is ignored rather than clamped: a list that has no current
	// row must not silently move the selection somewhere.

	controller.select_index ( XmlImportController::index_of_strategy ( XmlImportStrategyKind::GroupedAttributes ) );

	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"attributes\"" ) ) );

	controller.select_index ( -1 );
	controller.select_index ( 99 );

	QCOMPARE ( static_cast<int> ( controller.strategy () ),
	           static_cast<int> ( XmlImportStrategyKind::GroupedAttributes ) );
}

void TestXmlImportController::inferring_scalar_types_regenerates_the_preview ()
{
	XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"tag\": \"0\"" ) ) );

	controller.set_infer_scalar_types ( true );

	// The narrow inference rule: an integer becomes a number, and it stops there (section 2.11).

	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"tag\": 0" ) ) );
}

void TestXmlImportController::the_text_value_key_applies_to_custom_flattened_alone ()
{
	XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	QVERIFY ( !controller.text_value_key_applies () );

	controller.set_strategy ( XmlImportStrategyKind::CustomFlattened );

	QVERIFY ( controller.text_value_key_applies () );

	// Empty means the element's own name -- the documented default, stated as emptiness rather than as a literal.

	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"sentence\": \"The cat went up the hill.\"" ) ) );

	controller.set_text_value_key ( QStringLiteral ( "body" ) );

	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"body\": \"The cat went up the hill.\"" ) ) );

	// The key survives a round trip through another strategy, so a user who looks at BadgerFish and comes back has not
	// lost what they typed.

	controller.set_strategy ( XmlImportStrategyKind::BadgerFish );
	controller.set_strategy ( XmlImportStrategyKind::CustomFlattened );

	QCOMPARE ( controller.text_value_key (), QStringLiteral ( "body" ) );
	QVERIFY  ( controller.preview ().text.contains ( QStringLiteral ( "\"body\"" ) ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The error path and the answer
//---------------------------------------------------------------------------------------------------------------------

void TestXmlImportController::unparseable_xml_reports_a_position_and_refuses_the_import ()
{
	XmlImportController controller
	(
		QStringLiteral ( "<root><unclosed></root>" ),
		default_options (),
		default_profile ()
	);

	const XmlImportPreview& preview = controller.preview ();

	QVERIFY  ( !preview.ok );
	QVERIFY  ( preview.text.isEmpty () );
	QVERIFY  ( !preview.error.isEmpty () );
	QVERIFY  ( preview.error.contains ( QStringLiteral ( "line" ) ) );
	QVERIFY  ( preview.warnings.isEmpty () );
	QVERIFY  ( !controller.can_import () );

	// No strategy rescues a file that is not XML, so the refusal survives every one of them.

	for ( const XmlStrategyChoice& choice : XmlImportController::strategy_choices () )
	{
		controller.set_strategy ( choice.kind );

		QVERIFY ( !controller.can_import () );
		QVERIFY ( controller.preview ().text.isEmpty () );
	}
}

void TestXmlImportController::the_options_read_back_are_the_ones_that_were_set ()
{
	XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	QVERIFY ( controller.can_import () );

	controller.set_strategy           ( XmlImportStrategyKind::CustomFlattened );
	controller.set_infer_scalar_types ( true );
	controller.set_text_value_key     ( QStringLiteral ( "body" ) );

	const ImportOptions chosen = controller.options ();

	QCOMPARE ( static_cast<int> ( chosen.xmlStrategy ), static_cast<int> ( XmlImportStrategyKind::CustomFlattened ) );
	QCOMPARE ( chosen.xmlInferScalarTypes, true );
	QCOMPARE ( chosen.xmlTextValueKey, QStringLiteral ( "body" ) );

	// A Cancel needs nothing of the controller: the caller simply does not read options() back, and the persisted
	// preselection is left where it was. That the controller holds no reference to the settings store is what makes
	// that true by construction rather than by the pipeline remembering (pinned end to end in tst_file_controller).
}

//---------------------------------------------------------------------------------------------------------------------
// The degraded-construct notes
//---------------------------------------------------------------------------------------------------------------------

void TestXmlImportController::namespaces_and_mixed_content_are_reported_as_notes ()
{
	const QString xml = QStringLiteral
	(
		"<library xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
		"<book>Introduction <title>Deep Work</title> and a note.</book>"
		"</library>"
	);

	const XmlImportController controller ( xml, default_options (), default_profile () );

	const QStringList warnings = controller.preview ().warnings;

	QCOMPARE ( warnings.size (), 2 );

	// Namespaces first, because they are a property of the FILE and hold under every strategy; the count is the number
	// of elements that carried one (here the root's declaration alone).

	QVERIFY ( warnings [ 0 ].contains ( QStringLiteral ( "Namespaces" ) ) );
	QVERIFY ( warnings [ 0 ].contains ( QStringLiteral ( "1 element" ) ) );

	QVERIFY ( warnings [ 1 ].contains ( QStringLiteral ( "Mixed content" ) ) );
	QVERIFY ( warnings [ 1 ].contains ( QStringLiteral ( "1 element" ) ) );

	// A prefixed NAME is the other half of the namespace case, and the JSON keeps only the local name.

	const XmlImportController prefixed
	(
		QStringLiteral ( "<dc:record xmlns:dc=\"http://example.invalid/\"><dc:title>x</dc:title></dc:record>" ),
		default_options (),
		default_profile ()
	);

	QCOMPARE ( prefixed.preview ().warnings.size (), 1 );
	QVERIFY  ( prefixed.preview ().warnings [ 0 ].contains ( QStringLiteral ( "2 elements" ) ) );
	QVERIFY  ( prefixed.preview ().text.contains ( QStringLiteral ( "\"record\"" ) ) );
}

void TestXmlImportController::a_text_key_collision_is_reported_and_the_key_is_suffixed ()
{
	// The text key defaults to the element's own name, and this element has an attribute called "sentence" -- the
	// collision section 2.11 names. The warning and the renamed key are asserted in ONE case on purpose: the warning
	// mirrors CustomFlattenedStrategy's rule rather than sharing it, so the two are checked against each other here.

	const QString xml = QStringLiteral ( "<sentence sentence=\"attribute\">text content</sentence>" );

	XmlImportController controller ( xml, default_options (), default_profile () );

	QVERIFY ( controller.preview ().warnings.isEmpty () );     // Direct attribute keys has no such rule.

	controller.set_strategy ( XmlImportStrategyKind::CustomFlattened );

	const XmlImportPreview& preview = controller.preview ();

	QCOMPARE ( preview.warnings.size (), 1 );
	QVERIFY  ( preview.warnings [ 0 ].contains ( QStringLiteral ( "-text" ) ) );
	QVERIFY  ( preview.warnings [ 0 ].contains ( QStringLiteral ( "1 element" ) ) );

	QVERIFY ( preview.text.contains ( QStringLiteral ( "\"sentence-text\": \"text content\"" ) ) );

	// Naming the text member something that does not collide clears both.

	controller.set_text_value_key ( QStringLiteral ( "body" ) );

	QVERIFY ( controller.preview ().warnings.isEmpty () );
	QVERIFY ( controller.preview ().text.contains ( QStringLiteral ( "\"body\": \"text content\"" ) ) );
}

void TestXmlImportController::duplicate_keys_are_reported_for_the_strategy_that_causes_them ()
{
	// An attribute and a child element with the same name. Direct attribute keys puts both at the same level and so
	// repeats the key; BadgerFish prefixes the attribute and does not. The note is derived from the RESULT, which is
	// what lets one rule answer for all four strategies.

	const QString xml = QStringLiteral ( "<record title=\"attribute\"><title>element</title></record>" );

	XmlImportController controller ( xml, default_options (), default_profile () );

	QCOMPARE ( controller.preview ().warnings.size (), 1 );
	QVERIFY  ( controller.preview ().warnings [ 0 ].contains ( QStringLiteral ( "1 key" ) ) );

	controller.set_strategy ( XmlImportStrategyKind::BadgerFish );

	QVERIFY ( controller.preview ().warnings.isEmpty () );
}

void TestXmlImportController::a_large_rendering_is_truncated_with_a_note ()
{
	// Enough elements that the formatted JSON runs past the preview's line limit.

	QString xml = QStringLiteral ( "<catalogue>" );

	for ( int index = 0; index < config::xml_import::PREVIEW_MAXIMUM_LINES; ++index )
	{
		xml += QStringLiteral ( "<entry id=\"%1\"><name>row</name></entry>" ).arg ( index );
	}

	xml += QStringLiteral ( "</catalogue>" );

	const XmlImportController controller ( xml, default_options (), default_profile () );

	const XmlImportPreview& preview = controller.preview ();

	QVERIFY ( preview.ok );
	QVERIFY ( !preview.truncationNote.isEmpty () );

	QCOMPARE ( preview.text.split ( QLatin1Char ( '\n' ) ).size (), config::xml_import::PREVIEW_MAXIMUM_LINES );

	// The note names both counts, so the user can tell a preview that is nearly all of the file from one that is a
	// sliver of it, and says the truncation is the PREVIEW's rather than the import's.

	QVERIFY ( preview.truncationNote.contains ( QString::number ( config::xml_import::PREVIEW_MAXIMUM_LINES ) ) );
	QVERIFY ( preview.truncationNote.contains ( QStringLiteral ( "imported" ) ) );
}

void TestXmlImportController::a_clean_document_produces_no_notes_at_all ()
{
	// The common case, and the one worth guarding: a note that appears on ordinary input is worse than no notes,
	// because it trains the eye past the area where a real one will show up.

	const XmlImportController controller ( SENTENCE_XML, default_options (), default_profile () );

	QVERIFY ( controller.preview ().warnings.isEmpty () );
	QVERIFY ( controller.preview ().truncationNote.isEmpty () );
	QVERIFY ( controller.preview ().error.isEmpty () );
}

QTEST_MAIN ( TestXmlImportController )

#include "tst_xml_import_controller.moc"

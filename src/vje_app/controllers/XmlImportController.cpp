//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImportController implementation. See the header for why the XML is parsed once and why the preview is produced
//   by the import's own conversion rather than by a second rendering path.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/XmlImportController.hpp"

#include "AppConfig.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QHash>
#include <QLatin1Char>
#include <QObject>

#include <memory>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// The strategies in declaration order, which is the order section 2.11 tabulates them in.
		//-------------------------------------------------------------------------------------------------------------

		const std::vector<XmlImportStrategyKind>& strategy_kinds ()
		{
			static const std::vector<XmlImportStrategyKind> kinds =
			{
				XmlImportStrategyKind::BadgerFish,
				XmlImportStrategyKind::DirectAttributeKeys,
				XmlImportStrategyKind::GroupedAttributes,
				XmlImportStrategyKind::CustomFlattened
			};

			return kinds;
		}

		//-------------------------------------------------------------------------------------------------------------
		// Count phrases. Written out rather than left to "%n element(s)", because every one of these sentences is read
		// by a user deciding whether a warning matters to them, and a parenthesised plural reads as a placeholder.
		//
		// Each warning ENDS with its count for the same reason: a count in the middle of a sentence drags the verb
		// after it into agreement, and "1 element carry a prefix" is the shape that produces.
		//-------------------------------------------------------------------------------------------------------------

		QString element_phrase ( int count )
		{
			return ( count == 1 ) ? QObject::tr ( "1 element" ) : QObject::tr ( "%1 elements" ).arg ( count );
		}

		QString key_phrase ( int count )
		{
			return ( count == 1 ) ? QObject::tr ( "1 key" ) : QObject::tr ( "%1 keys" ).arg ( count );
		}

		//-------------------------------------------------------------------------------------------------------------
		// How many member keys the converted document repeats. Counted from the RESULT rather than simulated from the
		// strategy, which is what makes it exact for all four of them at once: whether an attribute collides with a
		// sibling element, or with the "content" key, or with nothing at all, is a property of what was built.
		//
		// Duplicate keys are legal JSON and VJE preserves them (VAL-02), so this is a note about fidelity, not an
		// error: the reader on the other side may keep only one of them.
		//-------------------------------------------------------------------------------------------------------------

		int count_duplicate_keys ( const JsonNode& node )
		{
			int duplicates = 0;

			if ( node.kind () == JsonKind::Object )
			{
				QHash<QString, int> seen;

				for ( int index = 0; index < node.member_count (); ++index )
				{
					const int occurrences = ++seen [ node.member_key ( index ) ];

					if ( occurrences > 1 )
					{
						++duplicates;
					}

					const JsonNode* const value = node.member_value ( index );

					if ( value != nullptr )
					{
						duplicates += count_duplicate_keys ( *value );
					}
				}
			}
			else if ( node.kind () == JsonKind::Array )
			{
				for ( int index = 0; index < node.array_size (); ++index )
				{
					const JsonNode* const element = node.array_element ( index );

					if ( element != nullptr )
					{
						duplicates += count_duplicate_keys ( *element );
					}
				}
			}

			return duplicates;
		}

		//-------------------------------------------------------------------------------------------------------------
		// Would Custom flattened have to rename this element's text member? MIRRORS CustomFlattenedStrategy's rule --
		// the text key is placed after the attributes and the child members, so it collides with an attribute name or a
		// child element name and with nothing else. Stated twice on purpose (there and here), because the strategy
		// builds a node and this builds a sentence; tst_xml_import_controller pins them together by asserting the
		// warning and the renamed key in the same case, so the pair cannot drift apart quietly.
		//-------------------------------------------------------------------------------------------------------------

		bool text_key_collides ( const XmlElement& element, const QString& configuredKey )
		{
			if ( element.text.isEmpty () || ( element.attributes.empty () && element.children.empty () ) )
			{
				return false;                                  // No text member at all, or a leaf that became a scalar.
			}

			const QString key = configuredKey.isEmpty () ? element.name : configuredKey;

			for ( const XmlAttribute& attribute : element.attributes )
			{
				if ( attribute.name == key )
				{
					return true;
				}
			}

			for ( const XmlElement& child : element.children )
			{
				if ( child.name == key )
				{
					return true;
				}
			}

			return false;
		}
	}

	//=================================================================================================================
	// XmlStrategyChoice
	//=================================================================================================================

	QString XmlStrategyChoice::labelled_name () const
	{
		if ( !recommended )
		{
			return displayName;
		}

		return QObject::tr ( "%1 (Recommended)" ).arg ( displayName );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	XmlImportController::XmlImportController
	(
		const QString&       xmlText,
		const ImportOptions& initialOptions,
		const FormatProfile& format
	)
	:	parsed           ( XmlImporter::parse ( xmlText ) ),
		format           ( format ),
		chosenStrategy   ( initialOptions.xmlStrategy ),
		inferScalarTypes ( initialOptions.xmlInferScalarTypes ),
		textValueKey     ( initialOptions.xmlTextValueKey )
	{
	}

	//=================================================================================================================
	// The strategy list
	//=================================================================================================================

	const std::vector<XmlStrategyChoice>& XmlImportController::strategy_choices ()
	{
		// Built once, by asking each strategy what it is called and what it does. Nothing here names a strategy, so a
		// wording change in vje_core reaches the dialog without a second edit -- and "(Recommended)" follows whichever
		// one answers is_default(), which is the same bit the pipeline falls back to.

		static const std::vector<XmlStrategyChoice> choices = [] ()
		{
			std::vector<XmlStrategyChoice> built;

			for ( const XmlImportStrategyKind kind : strategy_kinds () )
			{
				const std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy ( kind );

				XmlStrategyChoice choice;

				choice.kind        = kind;
				choice.displayName = strategy->display_name ();
				choice.description = strategy->description ();
				choice.recommended = strategy->is_default ();

				built.push_back ( choice );
			}

			return built;
		} ();

		return choices;
	}

	int XmlImportController::index_of_strategy ( XmlImportStrategyKind kind )
	{
		const std::vector<XmlStrategyChoice>& choices = strategy_choices ();

		for ( std::size_t index = 0; index < choices.size (); ++index )
		{
			if ( choices [ index ].kind == kind )
			{
				return static_cast<int> ( index );
			}
		}

		return 0;                                              // Unreachable: the list IS the enumeration.
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	int XmlImportController::selected_index () const
	{
		return index_of_strategy ( chosenStrategy );
	}

	XmlImportStrategyKind XmlImportController::strategy () const
	{
		return chosenStrategy;
	}

	bool XmlImportController::infer_scalar_types () const
	{
		return inferScalarTypes;
	}

	const QString& XmlImportController::text_value_key () const
	{
		return textValueKey;
	}

	bool XmlImportController::text_value_key_applies () const
	{
		return chosenStrategy == XmlImportStrategyKind::CustomFlattened;
	}

	ImportOptions XmlImportController::options () const
	{
		ImportOptions chosen;

		chosen.xmlStrategy         = chosenStrategy;
		chosen.xmlInferScalarTypes = inferScalarTypes;
		chosen.xmlTextValueKey     = textValueKey;

		return chosen;
	}

	bool XmlImportController::can_import () const
	{
		return parsed.ok;
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void XmlImportController::select_index ( int index )
	{
		const std::vector<XmlStrategyChoice>& choices = strategy_choices ();

		if ( ( index < 0 ) || ( index >= static_cast<int> ( choices.size () ) ) )
		{
			return;
		}

		set_strategy ( choices [ static_cast<std::size_t> ( index ) ].kind );
	}

	void XmlImportController::set_strategy ( XmlImportStrategyKind kind )
	{
		if ( kind == chosenStrategy )
		{
			return;
		}

		chosenStrategy = kind;
		previewStale   = true;
	}

	void XmlImportController::set_infer_scalar_types ( bool infer )
	{
		if ( infer == inferScalarTypes )
		{
			return;
		}

		inferScalarTypes = infer;
		previewStale     = true;
	}

	void XmlImportController::set_text_value_key ( const QString& key )
	{
		if ( key == textValueKey )
		{
			return;
		}

		textValueKey = key;

		// The key changes the conversion only under Custom flattened, but the preview is still marked stale under any
		// strategy: the alternative is a cache that is fresh or stale depending on which strategy was selected when the
		// field was typed in, and a wrong answer there is invisible until the user switches back.

		previewStale = true;
	}

	//=================================================================================================================
	// The preview
	//=================================================================================================================

	const XmlImportPreview& XmlImportController::preview () const
	{
		if ( previewStale )
		{
			regenerate ();

			previewStale = false;
		}

		return cachedPreview;
	}

	void XmlImportController::regenerate () const
	{
		cachedPreview = XmlImportPreview ();

		if ( !parsed.ok )
		{
			// No tree, so nothing any strategy could show. Reported here rather than through an error box, so the
			// reason stays on screen beside the file name while the user decides (section 2.11, FILE-06).

			cachedPreview.error = parsed.error;

			return;
		}

		const std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy ( chosenStrategy, textValueKey );

		// The IMPORT's own conversion and the SAVE's own formatter, so the preview is the result rather than a
		// rendering of it (SET-07 / FILE-03).

		const std::unique_ptr<JsonNode> converted = XmlImporter::convert_tree ( parsed.root, *strategy, inferScalarTypes );

		QString rendered = JsonFormatter::format ( *converted, format );

		cachedPreview.ok       = true;
		cachedPreview.warnings = collect_warnings ( *converted );

		// Truncation. The conversion is complete either way -- what is bounded is the text handed to the editor widget
		// and its highlighter, which is the cost that grows with the file (section 2.11).

		const QStringList lines = rendered.split ( QLatin1Char ( '\n' ) );

		if ( lines.size () > config::xml_import::PREVIEW_MAXIMUM_LINES )
		{
			rendered = lines.mid ( 0, config::xml_import::PREVIEW_MAXIMUM_LINES ).join ( QLatin1Char ( '\n' ) );

			cachedPreview.truncationNote = QObject::tr ( "Preview truncated: showing the first %1 lines of %2. The whole file is imported." )
			                                   .arg ( config::xml_import::PREVIEW_MAXIMUM_LINES )
			                                   .arg ( lines.size () );
		}

		cachedPreview.text = rendered;
	}

	QStringList XmlImportController::collect_warnings ( const JsonNode& converted ) const
	{
		QStringList warnings;

		int namespaced   = 0;
		int mixedContent = 0;
		int collisions   = 0;

		const bool customFlattened = ( chosenStrategy == XmlImportStrategyKind::CustomFlattened );

		visit_xml_elements ( parsed.root, [ & ] ( const XmlElement& element )
		{
			if ( element.namespaced )
			{
				++namespaced;
			}

			if ( !element.text.isEmpty () && !element.children.empty () )
			{
				++mixedContent;
			}

			if ( customFlattened && text_key_collides ( element, textValueKey ) )
			{
				++collisions;
			}
		} );

		// A fixed order, so switching strategies does not reshuffle the notes the user is reading. Namespaces first
		// because they are a property of the FILE (true under every strategy); the rest describe this conversion.

		if ( namespaced > 0 )
		{
			warnings.append
			(
				QObject::tr ( "Namespaces are not represented in JSON, so a prefixed name becomes its local name and a declaration is dropped (%1 affected)." )
				    .arg ( element_phrase ( namespaced ) )
			);
		}

		if ( mixedContent > 0 )
		{
			warnings.append
			(
				QObject::tr ( "Mixed content becomes a text member beside the child members, so the text loses its position among them (%1 affected)." )
				    .arg ( element_phrase ( mixedContent ) )
			);
		}

		if ( collisions > 0 )
		{
			warnings.append
			(
				QObject::tr ( "The text value key collides with an attribute or child name and takes a \"-text\" suffix (%1 affected)." )
				    .arg ( element_phrase ( collisions ) )
			);
		}

		const int duplicates = count_duplicate_keys ( converted );

		if ( duplicates > 0 )
		{
			warnings.append
			(
				QObject::tr ( "This strategy repeats a key already present in the same object; VJE keeps them all, but another tool may keep only the first (%1 affected)." )
				    .arg ( key_phrase ( duplicates ) )
			);
		}

		return warnings;
	}
}

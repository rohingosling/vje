//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CellPasteConverter implementation. See CellPasteConverter.hpp for the EDITOR-11 matrix and EDITOR-12 typed-entry
//   rules.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/CellPasteConverter.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/editing/edit_transforms.hpp>

namespace vje
{
	namespace
	{
		QString kind_name ( JsonKind kind )
		{
			switch ( kind )
			{
				case JsonKind::Null:    return QStringLiteral ( "null" );
				case JsonKind::Boolean: return QStringLiteral ( "boolean" );
				case JsonKind::Number:  return QStringLiteral ( "number" );
				case JsonKind::String:  return QStringLiteral ( "string" );
				case JsonKind::Array:   return QStringLiteral ( "array" );
				case JsonKind::Object:  return QStringLiteral ( "object" );
			}

			return QString ();
		}

		QString target_name ( CellTarget target )
		{
			switch ( target )
			{
				case CellTarget::String:  return QStringLiteral ( "string" );
				case CellTarget::Number:  return QStringLiteral ( "number" );
				case CellTarget::Boolean: return QStringLiteral ( "boolean" );
				case CellTarget::Object:  return QStringLiteral ( "object" );
				case CellTarget::Array:   return QStringLiteral ( "array" );
				case CellTarget::Untyped: return QStringLiteral ( "null" );
			}

			return QString ();
		}
	}

	//=================================================================================================================
	// Internals
	//=================================================================================================================

	CellPasteOutcome CellPasteConverter::converted ( std::unique_ptr<JsonNode> value )
	{
		CellPasteOutcome outcome;
		outcome.resolution = PasteResolution::Converted;
		outcome.value      = std::move ( value );
		return outcome;
	}

	CellPasteOutcome CellPasteConverter::incompatible ( const QString& sourceName, CellTarget target )
	{
		CellPasteOutcome outcome;
		outcome.resolution = PasteResolution::Incompatible;
		outcome.message    = QStringLiteral ( "Cannot paste a " ) + sourceName
		                   + QStringLiteral ( " value into a " ) + target_name ( target ) + QStringLiteral ( " cell." );
		return outcome;
	}

	//=================================================================================================================
	// Paste resolution (the conversion matrix)
	//=================================================================================================================

	CellPasteOutcome CellPasteConverter::resolve_paste ( const JsonNode& source, CellTarget target )
	{
		// null is universal: a null source pastes null into every column.

		if ( source.kind () == JsonKind::Null )
		{
			return converted ( JsonNode::make_null () );
		}

		// An untyped target (null / provisional / missing cell) takes the value as-is; a container as-is is still
		// shape-checked against the column.

		if ( target == CellTarget::Untyped )
		{
			if ( source.is_container () )
			{
				CellPasteOutcome outcome;
				outcome.resolution = PasteResolution::NeedsShapeCheck;
				outcome.value      = source.clone ();
				return outcome;
			}

			return converted ( source.clone () );
		}

		switch ( source.kind () )
		{
			case JsonKind::String:
			{
				const QString value = source.string_value ();

				switch ( target )
				{
					case CellTarget::String:
					{
						return converted ( source.clone () );
					}

					case CellTarget::Number:
					{
						const QString trimmed = value.trimmed ();

						if ( edit_transforms::is_json_number ( trimmed ) )
						{
							return converted ( JsonNode::make_number ( trimmed ) );
						}

						return incompatible ( kind_name ( JsonKind::String ), target );
					}

					case CellTarget::Boolean:
					{
						const QString lowered = value.trimmed ().toLower ();

						if ( lowered == QStringLiteral ( "true" ) )  { return converted ( JsonNode::make_boolean ( true  ) ); }
						if ( lowered == QStringLiteral ( "false" ) ) { return converted ( JsonNode::make_boolean ( false ) ); }

						return incompatible ( kind_name ( JsonKind::String ), target );
					}

					default:
					{
						return incompatible ( kind_name ( JsonKind::String ), target );
					}
				}
			}

			case JsonKind::Number:
			{
				switch ( target )
				{
					case CellTarget::String:
					{
						return converted ( JsonNode::make_string ( source.number_token () ) );   // Raw token as text.
					}

					case CellTarget::Number:
					{
						return converted ( source.clone () );
					}

					case CellTarget::Boolean:
					{
						const double magnitude = source.number_token ().toDouble ();

						if ( magnitude == 0.0 ) { return converted ( JsonNode::make_boolean ( false ) ); }
						if ( magnitude == 1.0 ) { return converted ( JsonNode::make_boolean ( true  ) ); }

						return incompatible ( kind_name ( JsonKind::Number ), target );
					}

					default:
					{
						return incompatible ( kind_name ( JsonKind::Number ), target );
					}
				}
			}

			case JsonKind::Boolean:
			{
				switch ( target )
				{
					case CellTarget::String:
					{
						return converted ( JsonNode::make_string ( source.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" ) ) );
					}

					case CellTarget::Number:
					{
						return converted ( JsonNode::make_number ( source.boolean_value () ? QStringLiteral ( "1" ) : QStringLiteral ( "0" ) ) );
					}

					case CellTarget::Boolean:
					{
						return converted ( source.clone () );
					}

					default:
					{
						return incompatible ( kind_name ( JsonKind::Boolean ), target );
					}
				}
			}

			case JsonKind::Object:
			{
				if ( target == CellTarget::Object )
				{
					CellPasteOutcome outcome;
					outcome.resolution = PasteResolution::NeedsShapeCheck;
					outcome.value      = source.clone ();
					return outcome;
				}

				return incompatible ( kind_name ( JsonKind::Object ), target );
			}

			case JsonKind::Array:
			{
				if ( target == CellTarget::Array )
				{
					CellPasteOutcome outcome;
					outcome.resolution = PasteResolution::NeedsShapeCheck;
					outcome.value      = source.clone ();
					return outcome;
				}

				return incompatible ( kind_name ( JsonKind::Array ), target );
			}

			case JsonKind::Null:
			{
				return converted ( JsonNode::make_null () );   // Unreachable (handled above); keeps the switch total.
			}
		}

		return incompatible ( kind_name ( source.kind () ), target );
	}

	//=================================================================================================================
	// Value resolution helpers
	//=================================================================================================================

	std::unique_ptr<JsonNode> CellPasteConverter::resolve_clipboard_text ( const QString& text )
	{
		ParseResult parsed = JsonParser::parse ( text );

		if ( parsed.ok )
		{
			return std::move ( parsed.root );              // Any JSON type, containers included.
		}

		return JsonNode::make_string ( text );             // Not valid JSON => a plain string.
	}

	std::unique_ptr<JsonNode> CellPasteConverter::interpret_typed_entry ( const QString& text )
	{
		ParseResult parsed = JsonParser::parse ( text.trimmed () );

		if ( parsed.ok && parsed.root->is_scalar () )
		{
			return std::move ( parsed.root );              // A JSON scalar literal (number keeps its raw token).
		}

		return JsonNode::make_string ( text );             // Anything else (incl. a bare container) commits as a string.
	}
}

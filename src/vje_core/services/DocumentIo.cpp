//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   DocumentIo implementation. See DocumentIo.hpp for the load/save contract (FILE-03 / FILE-06).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/DocumentIo.hpp>

#include <QFile>
#include <QStringDecoder>

namespace vje
{
	//=================================================================================================================
	// Load
	//=================================================================================================================

	LoadResult DocumentIo::load_text ( const QString& text )
	{
		ParseResult parsed = JsonParser::parse ( text );

		LoadResult result;
		result.root          = std::move ( parsed.root );
		result.ok            = parsed.ok;
		result.error         = parsed.error;
		result.duplicateKeys = std::move ( parsed.duplicateKeys );

		return result;
	}

	LoadResult DocumentIo::load_file ( const QString& path )
	{
		QFile file ( path );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			LoadResult result;
			result.ok            = false;
			result.error.message = QStringLiteral ( "Cannot open file: " ) + file.errorString ();
			return result;
		}

		const QByteArray bytes = file.readAll ();

		// Decode as UTF-8, tolerating (and discarding) a leading BOM. The parser sees clean text.

		QStringDecoder decoder ( QStringConverter::Utf8, QStringConverter::Flag::Default );
		const QString  text = decoder.decode ( bytes );

		return load_text ( text );
	}

	//=================================================================================================================
	// Save
	//=================================================================================================================

	QByteArray DocumentIo::save_bytes ( const JsonNode& root, const FormatProfile& profile )
	{
		QString text = JsonFormatter::format ( root, profile );

		// FILE-03: a single trailing newline; the formatter emits LF throughout and no trailing newline of its own.

		text += QLatin1Char ( '\n' );

		// UTF-8 without BOM (QString::toUtf8 never prepends a BOM).

		return text.toUtf8 ();
	}

	bool DocumentIo::save_file ( const QString& path, const JsonNode& root, const FormatProfile& profile, QString* errorMessage )
	{
		QFile file ( path );

		if ( !file.open ( QIODevice::WriteOnly | QIODevice::Truncate ) )
		{
			if ( errorMessage != nullptr )
			{
				*errorMessage = QStringLiteral ( "Cannot write file: " ) + file.errorString ();
			}

			return false;
		}

		const QByteArray bytes = save_bytes ( root, profile );

		if ( file.write ( bytes ) != bytes.size () )
		{
			if ( errorMessage != nullptr )
			{
				*errorMessage = QStringLiteral ( "Write failed: " ) + file.errorString ();
			}

			return false;
		}

		return true;
	}

	//=================================================================================================================
	// New document
	//=================================================================================================================

	std::unique_ptr<JsonNode> DocumentIo::new_document ()
	{
		return JsonNode::make_object ();
	}
}

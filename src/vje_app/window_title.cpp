//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   window_title implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "window_title.hpp"

#include "AppConfig.hpp"

namespace vje
{
	QString window_title ( const QString& applicationName, const QString& documentName, bool modified )
	{
		if ( documentName.isEmpty () )
		{
			// Nothing open. The separator and the marker go together -- a window with no document has nothing that
			// could be modified, so a marker here would name a document that is not there.

			return applicationName;
		}

		QString title = applicationName
		              + QString::fromUtf8 ( config::window::TITLE_SEPARATOR )
		              + documentName;

		if ( modified )
		{
			title += QString::fromUtf8 ( config::window::TITLE_MODIFIED_MARKER );
		}

		return title;
	}
}

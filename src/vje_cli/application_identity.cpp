//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   application_identity implementation -- see the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/application_identity.hpp>

#include <vje_core/version.hpp>

#include <QCoreApplication>

namespace vje::cli
{
	void apply_application_identity ()
	{
		QCoreApplication::setApplicationName    ( application_name () );
		QCoreApplication::setApplicationVersion ( version_string () );
		QCoreApplication::setOrganizationName   ( application_name () );
	}
}

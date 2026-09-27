//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   window_program implementation -- see the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/window_program.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace vje::cli
{
	QString window_program_path ()
	{
#if defined ( Q_OS_WIN )

		const QFileInfo self ( QCoreApplication::applicationFilePath () );

		return QDir ( self.absolutePath () ).filePath ( self.completeBaseName () + QStringLiteral ( ".exe" ) );

#else

		return QCoreApplication::applicationFilePath ();

#endif
	}
}

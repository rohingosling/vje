//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for the application icon AboutDialog presents beside its text (HELP-04, added 2026-08-13).
//
//   WHY THIS NEEDS A SUITE AT ALL. The application icon is the one asset in the project with no test behind it: it is
//   not part of either icon family, so tst_icon_library -- which walks the two families' resource trees -- does not
//   and should not see it. It reaches the screen through two routes, QApplication::setWindowIcon in main.cpp and this
//   dialog, and both fail the same silent way. A wrong resource prefix, an empty directory or a mis-sized request
//   yields a null or resampled pixmap, no error anywhere, and a dialog that simply looks slightly wrong.
//
//   THE SIZE ASSERTION IS THE LOAD-BEARING ONE, and it is not pedantry about a number. The icon is hand-drawn raster
//   (assets/images/icons/application-icon/), so it is sharp at the sizes it was drawn at and resampled at every other
//   one. QIcon answers a request it has no exact entry for by returning its largest entry BELOW it rather than
//   upscaling -- so asking for a size nobody drew comes back the wrong size, which is exactly what this reads.
//
//   WHAT IT DOES NOT COVER, said plainly. The dialog takes two QIcons of the same type as adjacent parameters, so the
//   CALL SITE can transpose them. This suite constructs the dialog itself and therefore cannot see that: it would
//   catch the resulting 35 px pixmap only if MainWindow's construction were driven from here, which would mean linking
//   IconLibrary and the whole window. It is left to manual smoke -- a transposed pair puts the help glyph in the
//   content and the product mark in the title bar, which is not subtle.
//
//   Offscreen: a dialog is constructed and its labels interrogated. Nothing is shown and nothing is drawn.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/AboutDialog.hpp"

#include "AppConfig.hpp"

#include <vje_core/version.hpp>

#include <QtTest/QtTest>

#include <QDir>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QRegularExpression>

using namespace vje;

namespace
{
	// The application icon, assembled the way main.cpp assembles it -- from every master in the resource directory.
	// Built here rather than taken from QApplication::windowIcon() so the case does not depend on a global the test
	// harness never sets, and so a broken resource prefix fails HERE rather than silently yielding an empty icon.

	QIcon application_icon ()
	{
		QIcon icon;

		const QDir directory ( QStringLiteral ( ":/vje/app" ) );

		for ( const QString& fileName : directory.entryList ( QDir::Files, QDir::Name ) )
		{
			icon.addFile ( directory.filePath ( fileName ) );
		}

		return icon;
	}

	// The content is RICH TEXT, and its markup runs through the words: the product expansion is spelled
	// "<b>V</b>ersatile <b>J</b>SON <b>E</b>ditor" so that each initial is emboldened. A plain substring search
	// therefore fails on text that is perfectly correct -- which is what the first version of this suite did. Stripping
	// the tags asks the question actually worth asking: what does a reader SEE?

	QString rendered_text ( const QString& richText )
	{
		static const QRegularExpression TAG ( QStringLiteral ( "<[^>]*>" ) );

		return QString ( richText ).remove ( TAG ).simplified ();
	}

	int opaque_pixel_count ( const QImage& image )
	{
		int count = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				if ( qAlpha ( image.pixel ( x, y ) ) > 0 ) ++count;
			}
		}

		return count;
	}
}

//*********************************************************************************************************************
// Class: TestAboutDialog
//*********************************************************************************************************************

class TestAboutDialog : public QObject
{
	Q_OBJECT

private slots:

	// THE ARTWORK IS COMPILED IN AT ALL. Asserted separately from the dialog, because every other case here would fail
	// identically if the resource were missing, and "the About dialog is broken" is a long way from "the icon did not
	// reach the binary".

	void the_application_icon_is_compiled_in ()
	{
		const QDir directory ( QStringLiteral ( ":/vje/app" ) );

		const QStringList files = directory.entryList ( QDir::Files, QDir::Name );

		QVERIFY2 ( !files.isEmpty (), "no application icon under :/vje/app -- check src/vje_app/CMakeLists.txt's glob" );

		QVERIFY2 ( !application_icon ().isNull (), "the application icon resource is present but yields a null QIcon" );
	}

	// THE DIALOG SHOWS IT, AT AN AUTHORED SIZE. Both halves matter and both fail separately: a null pixmap means the
	// icon never reached the label, and a pixmap of the wrong size means it was asked for at a size nobody drew.

	void the_about_dialog_shows_the_application_icon_at_its_authored_size ()
	{
		AboutDialog dialog ( QIcon (), application_icon () );

		QLabel* const label = dialog.icon_label ();

		QVERIFY2 ( label != nullptr, "the About dialog has no icon label" );

		const QPixmap pixmap = label->pixmap ( Qt::ReturnByValue );

		QVERIFY2 ( !pixmap.isNull (), "the About dialog's icon label carries no pixmap" );

		const int expected = config::dialog::ABOUT_ICON_SIZE;

		QVERIFY2
		(
			pixmap.size () == QSize ( expected, expected ),
			qPrintable ( QStringLiteral ( "the About icon came back %1x%2 rather than %3x%3 -- QIcon returns its "
			                              "largest entry BELOW an unmatched request, so there is no drawn master at "
			                              "that size" )
			             .arg ( pixmap.width () ).arg ( pixmap.height () ).arg ( expected ) )
		);

		// It renders, rather than merely existing at the right size. A transparent pixmap has a size too.

		QVERIFY2 ( opaque_pixel_count ( pixmap.toImage () ) > 0, "the About icon rendered blank" );
	}

	// The icon is an ADDITION, not a replacement. The phase that added it rebuilt the content area's layout, and the
	// text is what HELP-04 actually requires -- so this pins that none of it was lost on the way.

	void the_about_dialog_still_carries_its_text ()
	{
		AboutDialog dialog ( QIcon (), application_icon () );

		QLabel* const content = dialog.content_label ();

		QVERIFY2 ( content != nullptr, "the About dialog has no content label" );

		const QString text = rendered_text ( content->text () );

		QVERIFY2 ( text.contains ( QStringLiteral ( "Versatile JSON Editor" ) ), "the product expansion is missing (LD-8)" );
		QVERIFY2 ( text.contains ( version_string () ),                          "the version is missing" );

		// The link is read off the MARKUP rather than the rendered text, because an href is the thing HELP-04 asks for
		// and stripping the tags is exactly what would hide a link whose text no longer points anywhere.

		QVERIFY2 ( content->text ().contains ( QStringLiteral ( "href=\"https://github.com/rohingosling/vje\"" ) ),
		           "the project page link is missing or no longer a link" );

		// The third-party notice is a LICENCE OBLIGATION rather than a courtesy (Phase 15b.2): part of the Fluent icon
		// family is vendored from Microsoft under MIT, and the notice has to travel with the redistribution.

		QVERIFY2 ( text.contains ( QStringLiteral ( "Fluent System Icons" ) ),   "the Microsoft attribution is missing" );
		QVERIFY2 ( text.contains ( QStringLiteral ( "MIT" ) ),                   "the MIT licence is not named" );
	}

	// DECORATIVE, AND DELIBERATELY UNNAMED (NFR-05). The text beside it already says what the product is, so a name
	// here makes a screen reader announce it twice. Phase 14's sweep rule is that every control carries a name; this
	// is the stated exception, so it is asserted rather than left to be "fixed" by a later sweep.

	void the_about_icon_is_not_announced_twice ()
	{
		AboutDialog dialog ( QIcon (), application_icon () );

		QCOMPARE ( dialog.icon_label ()->accessibleName (), QString () );

		QVERIFY2 ( !dialog.content_label ()->accessibleName ().isEmpty (),
		           "the content label lost its accessible name -- that one is required" );
	}
};

QTEST_MAIN ( TestAboutDialog )

#include "tst_about_dialog.moc"

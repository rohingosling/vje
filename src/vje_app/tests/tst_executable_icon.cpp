//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for the executables' embedded application icon (FILE-15, architecture.md section 9.2). WINDOWS ONLY.
//
//   TWO CLAIMS, BECAUSE WINDOWS' OWN DECODING IS NOT EXACT. The icon entries must be the authored masters UNCHANGED,
//   which is read straight from the executable's RT_ICON resources and compared byte for byte with the PNG files. And
//   what Explorer draws -- the entries name `"vje.exe",0`, the executable's first icon -- must be each master at its
//   own size, which is asked of the shell's extraction call and compared pixel by pixel. The second comparison cannot
//   be exact: an icon handle holds PREMULTIPLIED pixels, and Windows premultiplies by truncating where Qt rounds, so a
//   semi-transparent pixel can differ by one step (measured 2026-09-26: the 16 px master's corners, alpha 0x54 over
//   grey 0x7F, came back 0x29 from Windows against Qt's 0x2A). Alpha must match exactly; each colour channel is allowed
//   that one step, and the byte comparison is what says nothing was resampled.
//
//   The masters are read from the source tree by directory rather than named, as CMake globs them: a newly drawn size
//   is covered the day it is added.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <QDir>
#include <QFile>
#include <QImage>
#include <QtTest/QtTest>

#include <windows.h>
#include <shellapi.h>

namespace
{
	const QString APPLICATION_ICON_DIRECTORY = QStringLiteral ( VJE_APPLICATION_ICON_DIRECTORY );

	// The window program only. The console twin carries no icon: the shell extracts none from a .com file whatever
	// it contains -- measured 2026-09-26, the twin's bytes with the resource compiled in reported no icon as vje.com
	// and one icon renamed .exe -- so embedding it there would change nothing anyone sees.

	QStringList executables ()
	{
		return { QStringLiteral ( VJE_WINDOW_PROGRAM ) };
	}

	// The executable's first icon at `size`, as the shell extracts it -- the entry of that size when there is one,
	// otherwise Windows' own scaling of the nearest. Null when there is no icon at all.

	QImage extracted_icon ( const QString& executable, int size )
	{
		HICON icon = nullptr;

		const UINT extracted = PrivateExtractIconsW ( reinterpret_cast<const wchar_t*> ( QDir::toNativeSeparators ( executable ).utf16 () ),
		                                              0, size, size, &icon, nullptr, 1, 0 );

		if ( ( extracted != 1 ) || ( icon == nullptr ) )
		{
			return QImage ();
		}

		const QImage image = QImage::fromHICON ( icon );

		DestroyIcon ( icon );

		return image.convertToFormat ( QImage::Format_ARGB32_Premultiplied );
	}

	// Every RT_ICON resource in the executable, as bytes. With the masters stored as PNG entries, each is one master
	// file exactly as it lies in the source tree.

	QList<QByteArray> icon_resources ( const QString& executable )
	{
		QList<QByteArray> resources;

		const HMODULE module = LoadLibraryExW ( reinterpret_cast<const wchar_t*> ( QDir::toNativeSeparators ( executable ).utf16 () ),
		                                        nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE );

		if ( module == nullptr )
		{
			return resources;
		}

		auto collect = [] ( HMODULE source, LPCWSTR type, LPWSTR name, LONG_PTR context ) -> BOOL
		{
			const HRSRC   found  = FindResourceW ( source, name, type );
			const HGLOBAL loaded = ( found != nullptr ) ? LoadResource ( source, found ) : nullptr;

			if ( loaded != nullptr )
			{
				reinterpret_cast<QList<QByteArray>*> ( context )->append
				(
					QByteArray ( static_cast<const char*> ( LockResource ( loaded ) ), static_cast<qsizetype> ( SizeofResource ( source, found ) ) )
				);
			}

			return TRUE;
		};

		EnumResourceNamesW ( module, RT_ICON, collect, reinterpret_cast<LONG_PTR> ( &resources ) );

		FreeLibrary ( module );

		return resources;
	}

	// Pixels that differ beyond premultiplication rounding, where either is not fully transparent -- a transparent
	// pixel's colour channels carry nothing and are not the artwork. Both images are premultiplied: alpha must match
	// exactly, and each colour channel may differ by one step (see the file header).

	int differing_pixels ( const QImage& actual, const QImage& expected )
	{
		int differing = 0;

		for ( int y = 0; y < expected.height (); ++y )
		{
			for ( int x = 0; x < expected.width (); ++x )
			{
				const QRgb left  = reinterpret_cast<const QRgb*> ( actual.constScanLine ( y ) ) [ x ];
				const QRgb right = reinterpret_cast<const QRgb*> ( expected.constScanLine ( y ) ) [ x ];

				if ( ( qAlpha ( left ) == 0 ) && ( qAlpha ( right ) == 0 ) )
				{
					continue;
				}

				const bool alphaDiffers  = qAlpha ( left ) != qAlpha ( right );
				const bool colourDiffers = ( qAbs ( qRed   ( left ) - qRed   ( right ) ) > 1 )
				                        || ( qAbs ( qGreen ( left ) - qGreen ( right ) ) > 1 )
				                        || ( qAbs ( qBlue  ( left ) - qBlue  ( right ) ) > 1 );

				if ( alphaDiffers || colourDiffers )
				{
					++differing;
				}
			}
		}

		return differing;
	}
}

class TestExecutableIcon : public QObject
{
	Q_OBJECT

private slots:

	void the_window_program_carries_exactly_one_icon ();
	void the_icon_entries_are_the_masters_unchanged ();
	void the_first_icon_is_every_master_as_drawn ();
};

void TestExecutableIcon::the_window_program_carries_exactly_one_icon ()
{
	// One icon group, so "vje.exe",0 -- what the Explorer entries name -- can only be the application icon.

	for ( const QString& executable : executables () )
	{
		const UINT icons = ExtractIconExW ( reinterpret_cast<const wchar_t*> ( QDir::toNativeSeparators ( executable ).utf16 () ), -1, nullptr, nullptr, 0 );

		QVERIFY2 ( icons == 1, qPrintable ( QStringLiteral ( "%1 carries %2 icons" ).arg ( executable ).arg ( icons ) ) );
	}
}

void TestExecutableIcon::the_icon_entries_are_the_masters_unchanged ()
{
	const QDir        directory ( APPLICATION_ICON_DIRECTORY );
	const QStringList masters = directory.entryList ( { QStringLiteral ( "*.png" ) }, QDir::Files, QDir::Name );

	QVERIFY ( !masters.isEmpty () );

	for ( const QString& executable : executables () )
	{
		const QList<QByteArray> resources = icon_resources ( executable );

		// One entry per master, and nothing else: a resampled size added beside them would be artwork nobody drew.

		QCOMPARE ( resources.size (), masters.size () );

		for ( const QString& fileName : masters )
		{
			QFile file ( directory.filePath ( fileName ) );

			QVERIFY ( file.open ( QIODevice::ReadOnly ) );
			QVERIFY2 ( resources.contains ( file.readAll () ), qPrintable ( fileName + QStringLiteral ( " is not embedded byte for byte" ) ) );
		}
	}
}

void TestExecutableIcon::the_first_icon_is_every_master_as_drawn ()
{
	const QStringList masters = QDir ( APPLICATION_ICON_DIRECTORY ).entryList ( { QStringLiteral ( "*.png" ) }, QDir::Files, QDir::Name );

	QVERIFY ( !masters.isEmpty () );

	for ( const QString& executable : executables () )
	{
		for ( const QString& fileName : masters )
		{
			// PREMULTIPLIED, both sides: an icon handle holds premultiplied pixels, which is what Windows draws from, and
			// converting them back to straight alpha would multiply the rounding step by 255 / alpha.

			const QImage master = QImage ( QDir ( APPLICATION_ICON_DIRECTORY ).filePath ( fileName ) ).convertToFormat ( QImage::Format_ARGB32_Premultiplied );

			QVERIFY2 ( !master.isNull (), qPrintable ( fileName ) );
			QCOMPARE ( master.width (), master.height () );

			const QImage icon = extracted_icon ( executable, master.width () );

			const QString where = QStringLiteral ( "%1 at %2 px (%3)" ).arg ( QFileInfo ( executable ).fileName () ).arg ( master.width () ).arg ( fileName );

			QVERIFY2 ( !icon.isNull (), qPrintable ( where + QStringLiteral ( ": no icon" ) ) );
			QCOMPARE ( icon.size (), master.size () );

			const int differing = differing_pixels ( icon, master );

			QVERIFY2 ( differing == 0, qPrintable ( where + QStringLiteral ( ": %1 pixels differ from the master" ).arg ( differing ) ) );
		}
	}
}

QTEST_GUILESS_MAIN ( TestExecutableIcon )

#include "tst_executable_icon.moc"

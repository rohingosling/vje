//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for the vje_core version API. Runs headlessly (QTEST_APPLESS_MAIN -- no QApplication, no
//   display), which is the property that keeps the core cheap to test. This is the
//   Phase 0 "trivial CTest" that proves configure -> build -> ctest is green end to end.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/version.hpp>

#include <QtTest/QtTest>

//---------------------------------------------------------------------------------------------------------------------
// Class: TestVersion
//
// Description:
//
//   Exercises application_name(), version_string(), and version_line().
//
//---------------------------------------------------------------------------------------------------------------------

class TestVersion : public QObject
{
	Q_OBJECT

private slots:

	void application_name_is_vje ();
	void version_string_is_non_empty ();
	void version_line_combines_name_and_version ();
};

void TestVersion::application_name_is_vje ()
{
	QCOMPARE ( vje::application_name (), QStringLiteral ( "VJE" ) );
}

void TestVersion::version_string_is_non_empty ()
{
	// The version comes from the CMake project() version -- it must be present and dot-separated.

	const QString version = vje::version_string ();

	QVERIFY ( !version.isEmpty () );
	QVERIFY ( version.contains ( QLatin1Char ( '.' ) ) );
}

void TestVersion::version_line_combines_name_and_version ()
{
	const QString expected = vje::application_name () + QLatin1Char ( ' ' ) + vje::version_string ();

	QCOMPARE ( vje::version_line (), expected );
}

QTEST_APPLESS_MAIN ( TestVersion )

#include "tst_version.moc"

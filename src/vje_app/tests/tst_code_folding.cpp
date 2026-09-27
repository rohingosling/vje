//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for code_folding -- the Code View's folding decisions (EDITOR-23), headless.
//
//   The claims:
//
//     - A REGION IS WHAT LIES BETWEEN A CONTAINER'S BRACKETS. Both bracket lines stay visible, so a folded object
//       still reads as one; a container whose brackets leave nothing between them -- inline, or on adjacent lines --
//       has no region at all.
//     - THE MARKER IS ON THE OPENING BRACKET'S LINE, which under Allman is the line BELOW a member's key. Measuring
//       from the key would hide the brace and leave a closing brace with no visible partner.
//     - ONE MARKER PER LINE, AND THE OUTER REGION HAS IT. Two containers opening on one line would otherwise put two
//       markers in one cell.
//     - A LINE HIDDEN BY TWO FOLDS NEEDS BOTH TO OPEN, and a reveal names every fold in its way, outermost first.
//     - THE KEYS ACT ON THE INNERMOST REGION IN THE STATE THEY CHANGE, so Fold pressed twice folds outward.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/code_folding.hpp"
#include "views/json_text_index.hpp"

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	// Allman, as the formatter writes it under the SET-07 default. Line numbers are in the margin for the reader.

	const char* const ALLMAN = R"({
  "name": "Alex",
  "profile":
  {
    "city": "Cape Town",
    "tags":
    [
      "a",
      "b"
    ]
  },
  "empty": {},
  "inline": [ 1, 2 ]
})";
	//  1  {
	//  2    "name"
	//  3    "profile":
	//  4    {
	//  5      "city"
	//  6      "tags":
	//  7      [
	//  8        "a",
	//  9        "b"
	// 10      ]
	// 11    },
	// 12    "empty": {},
	// 13    "inline": [ 1, 2 ]
	// 14  }

	const char* const KNR = R"({
  "profile": {
    "city": "Cape Town"
  },
  "pair": { "a": 1,
    "b": 2 }
})";
	// 1 {   2 "profile": {   3 "city"   4 },   5 "pair": { "a": 1,   6 "b": 2 }   7 }

	QList<FoldRegion> regions_of ( const char* text )
	{
		return fold_regions ( build_pointer_span_index ( QString::fromUtf8 ( text ) ) );
	}

	int index_of ( const QList<FoldRegion>& regions, const QString& pointer )
	{
		for ( int index = 0; index < regions.size (); ++index )
		{
			if ( regions [ index ].pointer == pointer )
			{
				return index;
			}
		}

		return -1;
	}
}

class TestCodeFolding : public QObject
{
	Q_OBJECT

private slots:

	//=================================================================================================================
	// Which regions a text has.
	//=================================================================================================================

	void regions_are_the_multi_line_containers ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		QCOMPARE ( regions.size (), 3 );

		QCOMPARE ( regions [ 0 ].pointer, QString () );                      // The root, lines 1..14.
		QCOMPARE ( regions [ 1 ].pointer, QStringLiteral ( "/profile" ) );
		QCOMPARE ( regions [ 2 ].pointer, QStringLiteral ( "/profile/tags" ) );

		// "empty": {} and "inline": [ 1, 2 ] have nothing between their brackets, and scalars never do.

		QCOMPARE ( index_of ( regions, QStringLiteral ( "/empty" ) ),  -1 );
		QCOMPARE ( index_of ( regions, QStringLiteral ( "/inline" ) ), -1 );
		QCOMPARE ( index_of ( regions, QStringLiteral ( "/name" ) ),   -1 );
	}

	void under_allman_the_marker_is_on_the_brace_not_the_key ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		const FoldRegion profile = regions [ index_of ( regions, QStringLiteral ( "/profile" ) ) ];

		QCOMPARE ( profile.openLine,  4 );    // "{" -- the key is on line 3.
		QCOMPARE ( profile.closeLine, 11 );

		const FoldRegion tags = regions [ index_of ( regions, QStringLiteral ( "/profile/tags" ) ) ];

		QCOMPARE ( tags.openLine,  7 );
		QCOMPARE ( tags.closeLine, 10 );
	}

	void under_knr_the_marker_is_on_the_key_line ()
	{
		const QList<FoldRegion> regions = regions_of ( KNR );

		const int profile = index_of ( regions, QStringLiteral ( "/profile" ) );

		QVERIFY ( profile >= 0 );

		QCOMPARE ( regions [ profile ].openLine,  2 );
		QCOMPARE ( regions [ profile ].closeLine, 4 );

		// Brackets on adjacent lines -- "{ "a": 1," then ""b": 2 }" -- leave no line between them to hide.

		QCOMPARE ( index_of ( regions, QStringLiteral ( "/pair" ) ), -1 );
	}

	void two_containers_opening_on_one_line_give_the_marker_to_the_outer ()
	{
		const QList<FoldRegion> regions = regions_of ( "[ {\n  \"a\": 1,\n  \"b\": 2\n}, {\n  \"c\": 3\n} ]" );

		// Line 1 opens both the root array and element 0; line 4 opens element 1 (and closes element 0).

		QCOMPARE ( regions.size (), 2 );
		QCOMPARE ( regions [ 0 ].pointer,  QString () );
		QCOMPARE ( regions [ 0 ].openLine, 1 );
		QCOMPARE ( regions [ 1 ].pointer,  QStringLiteral ( "/1" ) );
		QCOMPARE ( regions [ 1 ].openLine, 4 );
	}

	void a_region_below_a_syntax_error_is_not_found ()
	{
		// The index is silent below an error -- which is why the Code View acts only on a COMPLETE index. Here the
		// container that encloses the error is not a region either: its scan never reached its closing bracket.

		bool complete = true;

		const PointerSpanIndex index = build_pointer_span_index
		(
			QStringLiteral ( "{\n  \"a\":\n  [\n    1\n  ],\n  \"b\": ,\n  \"c\":\n  [\n    2\n  ]\n}" ),
			&complete
		);

		QVERIFY ( !complete );

		const QList<FoldRegion> regions = fold_regions ( index );

		QCOMPARE ( regions.size (), 1 );
		QCOMPARE ( regions [ 0 ].pointer, QStringLiteral ( "/a" ) );
	}

	//=================================================================================================================
	// Which lines a set of folds leaves visible.
	//=================================================================================================================

	void a_fold_hides_its_interior_and_keeps_both_brackets ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );
		const QList<bool>       visible = visible_lines ( 14, regions, { QStringLiteral ( "/profile" ) } );

		QCOMPARE ( visible.size (), 14 );

		for ( int line = 1; line <= 14; ++line )
		{
			const bool hidden = ( line >= 5 ) && ( line <= 10 );

			QVERIFY2 ( visible [ line - 1 ] == !hidden, qPrintable ( QStringLiteral ( "line %1" ).arg ( line ) ) );
		}
	}

	void nothing_folded_hides_nothing ()
	{
		const QList<bool> visible = visible_lines ( 14, regions_of ( ALLMAN ), {} );

		QVERIFY ( !visible.contains ( false ) );
	}

	void a_line_inside_two_folds_stays_hidden_until_both_open ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		const QSet<QString> both { QStringLiteral ( "/profile" ), QStringLiteral ( "/profile/tags" ) };

		QVERIFY ( !visible_lines ( 14, regions, both ) [ 7 ] );                                       // Line 8.
		QVERIFY ( !visible_lines ( 14, regions, { QStringLiteral ( "/profile/tags" ) } ) [ 7 ] );    // Inner alone.
		QVERIFY ( !visible_lines ( 14, regions, { QStringLiteral ( "/profile" ) } ) [ 7 ] );         // Outer alone.

		// And opening the outer one leaves the inner fold exactly as it was: its brackets show, its interior does not.

		const QList<bool> innerOnly = visible_lines ( 14, regions, { QStringLiteral ( "/profile/tags" ) } );

		QVERIFY (  innerOnly [ 6 ] );      // Line 7, "[".
		QVERIFY ( !innerOnly [ 8 ] );      // Line 9, "b".
		QVERIFY (  innerOnly [ 9 ] );      // Line 10, "]".
	}

	void a_fold_naming_no_region_hides_nothing ()
	{
		// A pointer the current text has no region for -- a node deleted, or collapsed onto one line -- is inert.

		const QList<bool> visible = visible_lines ( 14, regions_of ( ALLMAN ), { QStringLiteral ( "/gone" ), QStringLiteral ( "/empty" ) } );

		QVERIFY ( !visible.contains ( false ) );
	}

	//=================================================================================================================
	// Reveal, and the keys.
	//=================================================================================================================

	void a_reveal_names_every_fold_in_the_way_outermost_first ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		const QSet<QString> both { QString (), QStringLiteral ( "/profile" ), QStringLiteral ( "/profile/tags" ) };

		QCOMPARE
		(
			folds_hiding_line ( regions, both, 8 ),
			QStringList ( { QString (), QStringLiteral ( "/profile" ), QStringLiteral ( "/profile/tags" ) } )
		);

		// A bracket line is never hidden by its own fold, so revealing one opens only the folds AROUND it.

		QCOMPARE ( folds_hiding_line ( regions, both, 7 ), QStringList ( { QString (), QStringLiteral ( "/profile" ) } ) );

		// A visible line needs nothing opened.

		QVERIFY ( folds_hiding_line ( regions, {}, 8 ).isEmpty () );
	}

	void fold_takes_the_innermost_open_region_and_then_the_next_out ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		const int first = innermost_region_at_line ( regions, {}, 8, false );

		QCOMPARE ( regions [ first ].pointer, QStringLiteral ( "/profile/tags" ) );

		const int second = innermost_region_at_line ( regions, { QStringLiteral ( "/profile/tags" ) }, 7, false );

		QCOMPARE ( regions [ second ].pointer, QStringLiteral ( "/profile" ) );
	}

	void unfold_takes_the_innermost_folded_region ()
	{
		const QList<FoldRegion> regions = regions_of ( ALLMAN );

		const QSet<QString> both { QStringLiteral ( "/profile" ), QStringLiteral ( "/profile/tags" ) };

		QCOMPARE ( regions [ innermost_region_at_line ( regions, both, 4, true ) ].pointer, QStringLiteral ( "/profile" ) );
		QCOMPARE ( innermost_region_at_line ( regions, {}, 4, true ), -1 );
	}
};

QTEST_APPLESS_MAIN ( TestCodeFolding )

#include "tst_code_folding.moc"

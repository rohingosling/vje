//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_icon_library -- the icon set, as TWO FAMILIES picked by Interface style (SET-13 / SET-13a).
//
//   REWRITTEN 2026-08-12. The suite it replaces asserted that the two committed trees were the SAME ARTWORK in two
//   formats, which is the property the restructure deliberately ended: Classic and Fluent are now different drawings
//   with different rules, and a test demanding they match would be demanding the feature back.
//
//   WHAT THIS SUITE EXISTS FOR is the set of failures that are silent on screen:
//
//     1. A NAME MISSING FROM ONE FAMILY. Switching Interface style would drop that command's icon and nothing else,
//        which nobody notices until they switch. Asserted per family, over the authoritative name list.
//     2. A RUNG SERVED BY THE WRONG SIZE. QIcon scales silently and actualSize reports the size ASKED FOR, so a
//        resampled glyph looks like poor artwork rather than a wrong number. Asserted by measuring what comes back.
//     3. A PARTIAL MODE TABLE. QIcon matches MODE first and SIZE second, so an absent Disabled entry at one size does
//        not fall back to that size's Normal pixmap -- it reaches for another SIZE's Disabled entry and scales it.
//        This shipped, and made the 16 artwork invisible until a document was open.
//     4. CLASSIC BEING TINTED. It is artwork; the palette must not touch it, or the colour work is discarded exactly
//        where a command is unavailable.
//     5. FLUENT NOT BEING TINTED. It is a mask set; if currentColor is not substituted the glyph renders near-black
//        in both themes, which is invisible against Dark.
//     6. THE CUSTOM SVGs DRIFTING from the reference PNGs they are traced from -- the one derived asset left.
//
//   NOTHING HERE PINS THE COLOUR PARTITION. The Classic set is being coloured glyph by glyph and size by size, so a
//   case asserting "22 of 47 are multicolour" is a tripwire that fails on the next glyph drawn. The partition is
//   REPORTED (qInfo) and the RULES are asserted.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/IconLibrary.hpp"
#include "services/ThemeService.hpp"

#include "AppConfig.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

using config::appearance::InterfaceStyle;

namespace
{
	// The authoritative name list: every constant in icon_names. 49 in total, and it is a CROSS-FAMILY invariant --
	// both families must carry all of them.
	//
	// The four view-tab glyphs used to be spelled here as STRING LITERALS, because their views spelled them that way
	// too. That duplication is how the JSONPath glyph went missing on 2026-08-14: the provider said "vje-view-query",
	// the artwork said "vje-view-jsonpath", and no test could tell, because a name the set does not hold renders as a
	// bare label rather than as a failure. They are constants now (2026-08-16), so this list and the providers cannot
	// disagree about a name again.

	QStringList every_icon_name ()
	{
		return
		{
			icon_names::DOCUMENT_NEW,   icon_names::DOCUMENT_OPEN,    icon_names::DOCUMENT_SAVE,
			icon_names::DOCUMENT_SAVE_AS, icon_names::DOCUMENT_CLOSE, icon_names::DOCUMENT_PRINT,
			icon_names::DOCUMENT_PAGE_SETUP,

			icon_names::EDIT_UNDO,      icon_names::EDIT_REDO,        icon_names::EDIT_CUT,
			icon_names::EDIT_COPY,      icon_names::EDIT_PASTE,       icon_names::EDIT_FIND,
			icon_names::GO_TO,          icon_names::COPY_POINTER,     icon_names::COPY_QUERY_RESULT,

			icon_names::NODE_DELETE,    icon_names::NODE_DUPLICATE,   icon_names::NODE_RENAME,
			icon_names::NODE_MOVE_UP,   icon_names::NODE_MOVE_DOWN,

			icon_names::NORMALIZE_ARRAY, icon_names::ARRAY_TO_OBJECTS, icon_names::OBJECTS_TO_ARRAY,

			icon_names::CONVERT_STRING, icon_names::CONVERT_NUMBER,   icon_names::CONVERT_BOOLEAN,
			icon_names::CONVERT_NULL,

			icon_names::EXPAND_ALL,     icon_names::COLLAPSE_ALL,
			icon_names::SETTINGS,       icon_names::ABOUT,

			icon_names::TYPE_DOCUMENT,
			icon_names::TYPE_OBJECT,    icon_names::TYPE_ARRAY,       icon_names::TYPE_STRING,
			icon_names::TYPE_NUMBER,    icon_names::TYPE_BOOLEAN,     icon_names::TYPE_NULL,

			icon_names::ADD_OBJECT,     icon_names::ADD_ARRAY,        icon_names::ADD_STRING,
			icon_names::ADD_NUMBER,     icon_names::ADD_BOOLEAN,      icon_names::ADD_NULL,

			icon_names::VIEW_FORM,      icon_names::VIEW_TEXT,        icon_names::VIEW_CODE,
			icon_names::VIEW_JSONPATH,

			// Chrome rather than a command, and the only name here no QAction carries (VIEW-03).

			icon_names::TAB_CLOSE
		};
	}

	//-----------------------------------------------------------------------------------------------------------------
	// PENDING CLASSIC RUNGS -- artwork the author has not drawn yet, declared so the suite stays a tripwire.
	//-----------------------------------------------------------------------------------------------------------------
	//
	// THE RUNGS ABOVE 20 SERVE 125-175% DISPLAY SCALING (config::icons::LADDER). At 100% nothing ever asks for them,
	// so a glyph that exists at 16 and 20 is complete for the way this application is developed and smoke tested, and
	// the remaining five rungs are drawn as the author gets to them. That is a long tail by agreement rather than an
	// oversight -- and a suite left red for the length of it stops being read, which is exactly how a NEW gap comes to
	// arrive looking like an old one (development-plan section 5's red-rot warning, aimed at the Linux CI build).
	//
	// So the gap is DECLARED rather than tolerated, and the declaration is checked in BOTH DIRECTIONS -- lesson D23,
	// where a table serving as both specification and oracle let a self-consistent wrong answer satisfy both roles:
	//
	//   absent  and listed    -> passes, and is counted into a qInfo line so the remaining work is visible every run.
	//   absent  and UNLISTED  -> FAILS. A newly-added glyph missing its 16 or 20 is a real defect and still says so.
	//   present and listed    -> FAILS. The list has gone stale: a rung was drawn and nothing recorded it.
	//
	// The third case is the one that earns the mechanism. Finishing a drawing turns the suite red until this list and
	// the icon progress tracker are updated together, which is the moment that record is worth writing and the moment
	// it would otherwise be forgotten.
	//
	// THE RUNTIME CONSEQUENCE IS NIL AND IS NOT WHAT THIS BOUNDS. IconLibrary::build_icon walks the ladder and
	// add_raster_size treats an absent file as "skip this rung", so at 150% these three glyphs are resampled from
	// their 20 px master -- slightly soft, never blank. What is pending is sharpness at a scaling factor this project
	// does not develop at, which is why shipping ahead of it is a decision rather than a compromise.

	QString pending_key ( const QString& name, int deviceSize )
	{
		return QStringLiteral ( "%1@%2" ).arg ( name ).arg ( deviceSize );
	}

	const QSet<QString>& pending_classic_rungs ()
	{
		static const QSet<QString> pending
		{
			// Copy JSONPath Result (QUERY-08), drawn at 16 and 20 on 2026-08-16.

			QStringLiteral ( "jsonpath-copy-result@24" ),
			QStringLiteral ( "jsonpath-copy-result@25" ),
			QStringLiteral ( "jsonpath-copy-result@28" ),
			QStringLiteral ( "jsonpath-copy-result@30" ),
			QStringLiteral ( "jsonpath-copy-result@35" ),

			// The tab close button (Phase 15c), hand-drawn at 16 and 20 on 2026-08-16, superseding the resampled set.

			QStringLiteral ( "tab-close@28" ),
			QStringLiteral ( "tab-close@30" ),
			QStringLiteral ( "tab-close@35" ),

			// The JSONPath view tab (QUERY-01), drawn at 16, 20, 24 and 25 on 2026-08-16.

			QStringLiteral ( "vje-view-jsonpath@28" ),
			QStringLiteral ( "vje-view-jsonpath@30" ),
			QStringLiteral ( "vje-view-jsonpath@35" ),
		};

		return pending;
	}

	// The resource path shape, stated ONCE for the suite exactly as IconLibrary states it once for the application.
	// The superseded suite spelled it in four string literals, which is how a test comes to assert a layout the code
	// stopped using.

	QString classic_directory ( int deviceSize )
	{
		return QStringLiteral ( ":/vje/icons/classic/%1" ).arg ( deviceSize );
	}

	QString microsoft_directory ( int designSize )
	{
		return QStringLiteral ( ":/vje/icons/fluent/microsoft/%1" ).arg ( designSize );
	}

	QString custom_directory ( int designSize )
	{
		return QStringLiteral ( ":/vje/icons/fluent/custom/%1" ).arg ( designSize );
	}

	// The names assets/images/icons/fluent/microsoft/icon-map.json says Microsoft serves. Read from the map rather than
	// from the vendored directory, because the two are exactly what the cases below compare: a listing derived from the
	// tree under test can only ever agree with it (lessons-learned D23).
	//
	// The map states the partition BY OMISSION -- a name absent from it is custom-backed -- so this one read answers
	// both halves, and there is no second list to fall out of step.

	QStringList vendored_names_from_the_map ()
	{
		QFile file ( QStringLiteral ( ":/vje/test/icon-map.json" ) );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			return {};
		}

		const QJsonObject icons =
			QJsonDocument::fromJson ( file.readAll () ).object ().value ( QStringLiteral ( "icons" ) ).toObject ();

		QStringList names = icons.keys ();

		names.sort ();

		return names;
	}

	QString reference_png_path ( const QString& name, int designSize )
	{
		return QStringLiteral ( ":/vje/test/icon-reference/%1/%2.png" ).arg ( designSize ).arg ( name );
	}

	// Does this tree carry the tracer's INPUT at all? It is present in the private tree, where the tracer runs, and
	// deliberately absent from the published one -- the manifest excludes svg-reference-png, because shipping the
	// inputs alongside the SVGs traced from them publishes the same artwork twice with one copy dead.
	//
	// This asks whether the tree is there, NOT whether a particular file is. A partially-present tree must still fail
	// the case: "this is a published tree" and "someone deleted a reference PNG" are the two states the drift check
	// exists to tell apart, and collapsing them would let the second hide behind the first.

	bool reference_tree_is_present ()
	{
		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			const QDir directory ( QStringLiteral ( ":/vje/test/icon-reference/%1" ).arg ( designSize ) );

			if ( !directory.entryList ( { QStringLiteral ( "*.png" ) }, QDir::Files ).isEmpty () )
			{
				return true;
			}
		}

		return false;
	}

	QStringList base_names_in ( const QString& directoryPath, const QString& suffix )
	{
		QStringList names;

		const QDir directory ( directoryPath );

		for ( const QString& entry : directory.entryList ( { QStringLiteral ( "*%1" ).arg ( suffix ) }, QDir::Files ) )
		{
			names.append ( entry.left ( entry.size () - suffix.size () ) );
		}

		names.sort ();

		return names;
	}

	int opaque_pixel_count ( const QImage& image )
	{
		int count = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				if ( qAlpha ( image.pixel ( x, y ) ) > 0 )
				{
					++count;
				}
			}
		}

		return count;
	}

	// How many inked pixels carry HUE -- r, g and b not all equal. This is what tells a drawing from a grey ramp, and
	// it is the measurement both tinting cases turn on.

	int hued_pixel_count ( const QImage& image )
	{
		int count = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				const QRgb pixel = image.pixel ( x, y );

				if ( qAlpha ( pixel ) == 0 )
				{
					continue;
				}

				if ( ( qRed ( pixel ) != qGreen ( pixel ) ) || ( qGreen ( pixel ) != qBlue ( pixel ) ) )
				{
					++count;
				}
			}
		}

		return count;
	}

	// Total coverage. A fade multiplies every pixel's alpha, so the sum is the measurement that distinguishes fading
	// from not fading -- where a pixel-difference test is satisfied by any change at all.

	qint64 summed_alpha ( const QImage& image )
	{
		qint64 total = 0;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				total += qAlpha ( image.pixel ( x, y ) );
			}
		}

		return total;
	}

	// The colours of the FULLY OPAQUE pixels, and only those.
	//
	// REVISED 2026-08-14, and the reason is worth stating because the old form looked stricter and was not. It took
	// every pixel with any alpha at all, which is bit-exact only while every Classic master has BINARY alpha -- true of
	// the whole set until tab-close arrived with real antialiasing. A partially transparent pixel does not survive a
	// QPixmap round trip unchanged: premultiplying by alpha and dividing back out is lossy, and #880015 at alpha 84
	// returns as #890015. That is Qt's arithmetic, not a tint, and a case that fails on it is reporting the wrong thing.
	//
	// Restricting to the opaque core keeps the claim the case actually makes -- Classic artwork is not recoloured,
	// which any tint would show here -- and the ANTIALIASING is pinned separately by alpha_channels_match, which is
	// exact because alpha itself is carried through untouched.

	QSet<QRgb> opaque_colours ( const QImage& image )
	{
		QSet<QRgb> colours;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				const QRgb pixel = image.pixel ( x, y );

				if ( qAlpha ( pixel ) == 255 )
				{
					colours.insert ( pixel | 0xFF000000u );
				}
			}
		}

		return colours;
	}

	bool alpha_channels_match ( const QImage& left, const QImage& right )
	{
		if ( left.size () != right.size () )
		{
			return false;
		}

		for ( int y = 0; y < left.height (); ++y )
		{
			for ( int x = 0; x < left.width (); ++x )
			{
				if ( qAlpha ( left.pixel ( x, y ) ) != qAlpha ( right.pixel ( x, y ) ) )
				{
					return false;
				}
			}
		}

		return true;
	}

}

//*********************************************************************************************************************
// Class: TestIconLibrary
//*********************************************************************************************************************

class TestIconLibrary : public QObject
{
	Q_OBJECT

	//=================================================================================================================
	// Test Cases
	//=================================================================================================================

private slots:

	void init ()
	{
		temporaryDirectory = std::make_unique<QTemporaryDir> ();

		settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
		theme    = std::make_unique<ThemeService>  ( settings.get () );
		icons    = std::make_unique<IconLibrary>   ( theme.get () );

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	void cleanup ()
	{
		// Reverse construction order (lesson Q1).

		icons.reset ();
		theme.reset ();
		settings.reset ();

		temporaryDirectory.reset ();
	}

	//=================================================================================================================
	// Completeness -- failure 1
	//=================================================================================================================

	// EVERY FAMILY CARRIES EVERY NAME. This replaces every_master_and_both_sources_carry_every_name, whose claim was
	// that the two TREES agreed; the claim now is that the two FAMILIES do, which is what makes Interface style safe
	// to switch. A name missing from one family drops that command's icon and only that one.

	void every_family_carries_every_name ()
	{
		const QStringList expected = [] { QStringList names = every_icon_name (); names.sort (); return names; } ();

		// Classic: one drawn file per rung, so every rung must carry every name -- EXCEPT the rungs declared pending
		// above, which are checked in both directions so the declaration cannot quietly go stale.

		int pendingFound = 0;

		for ( const config::icons::Rung& rung : config::icons::LADDER )
		{
			const QStringList found = base_names_in ( classic_directory ( rung.deviceSize ), QStringLiteral ( ".png" ) );

			for ( const QString& name : expected )
			{
				const bool isPending = pending_classic_rungs ().contains ( pending_key ( name, rung.deviceSize ) );

				if ( isPending )
				{
					// THE STALE-LIST DIRECTION. A rung that has been drawn must leave the list, because a declaration
					// nobody prunes stops describing the tree and starts excusing it.

					QVERIFY2
					(
						!found.contains ( name ),
						qPrintable ( QStringLiteral ( "Classic %1 px NOW HAS %2 -- drawn, so remove it from "
						                              "pending_classic_rungs() and mark it in the progress tracker" )
						             .arg ( rung.deviceSize ).arg ( name ) )
					);

					++pendingFound;

					continue;
				}

				QVERIFY2
				(
					found.contains ( name ),
					qPrintable ( QStringLiteral ( "Classic %1 px has no %2" ).arg ( rung.deviceSize ).arg ( name ) )
				);
			}
		}

		// Reported every run, so the remaining work is visible rather than remembered. Deliberately not asserted as a
		// COUNT: a pinned number is a tripwire that fires on every glyph the author finishes, which is the mistake the
		// Classic colour partition is reported-and-never-asserted to avoid.

		qInfo ()
			<< "Classic rungs pending (125-175% scaling only):" << pendingFound
			<< "of" << pending_classic_rungs ().size () << "declared";

		// And every declared key must have been VISITED. A typo in the list -- a name the set does not hold, or a rung
		// off the ladder -- would otherwise sit there forever excusing nothing.

		QCOMPARE ( pendingFound, static_cast<int> ( pending_classic_rungs ().size () ) );

		// Fluent: the UNION of the two sub-sources at each design size. Microsoft wins where it has a glyph and the
		// custom tree is the fallback bed, so completeness is a property of the pair rather than of either.

		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			QStringList found = base_names_in ( microsoft_directory ( designSize ), QStringLiteral ( ".svg" ) );

			found += base_names_in ( custom_directory ( designSize ), QStringLiteral ( ".svg" ) );

			for ( const QString& name : expected )
			{
				QVERIFY2
				(
					found.contains ( name ),
					qPrintable ( QStringLiteral ( "Fluent %1 px design has no %2 in either sub-source" )
					             .arg ( designSize ).arg ( name ) )
				);
			}
		}
	}

	// The two Fluent sub-sources must PARTITION the set. Microsoft silently wins a collision, so a name in both means
	// the custom artwork is never seen -- and the only symptom is that one glyph looking different from the rest.

	// NOTE, REVISED BY PHASE 15b.2 -- and the revision corrects a prediction rather than discharging it.
	//
	// 15b.1 recorded that reversing the Microsoft-before-custom PRECEDENCE failed nothing, because fluent/microsoft/
	// was empty, and said the precedence "gets its case with the first vendored glyph". It does not. The vendoring
	// landed and the precedence is STILL unfalsifiable, for a better reason: this case forces the two sub-sources to
	// PARTITION the names, and a precedence rule only ever decides an overlap. The two claims are mutually exclusive
	// by construction -- whichever order IconLibrary probes in, a partitioned set resolves identically.
	//
	// So the precedence is a safety net for a state this case forbids, and THIS is the live guarantee. Written down
	// rather than left as a to-do, because a to-do that cannot be done reads like coverage that has not arrived yet.

	void no_name_is_served_by_both_fluent_sub_sources ()
	{
		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			const QStringList vendored = base_names_in ( microsoft_directory ( designSize ), QStringLiteral ( ".svg" ) );
			const QStringList custom   = base_names_in ( custom_directory    ( designSize ), QStringLiteral ( ".svg" ) );

			for ( const QString& name : vendored )
			{
				QVERIFY2
				(
					!custom.contains ( name ),
					qPrintable ( QStringLiteral ( "%1 exists in BOTH Fluent sub-sources at %2 px -- Microsoft wins and "
					                              "the custom glyph is unreachable" ).arg ( name ).arg ( designSize ) )
				);
			}
		}
	}

	//=================================================================================================================
	// The vendoring (Phase 15b.2)
	//=================================================================================================================

	// THE MAP AND THE VENDORED TREE AGREE, IN BOTH DIRECTIONS.
	//
	// Forwards: every mapped name is a file under VJE's own name, at BOTH design sizes. A name that resolved upstream
	// but never landed would be silently custom-backed, which the partition case above cannot see -- it only knows the
	// name is not in both places, and "in neither of the places it should be" satisfies that too.
	//
	// Backwards: every vendored file is named by the map. A vendored file the map no longer mentions is one the
	// puller's sweep should have removed, and it would keep shadowing the custom glyph that had just been restored.
	//
	// BOTH DESIGN SIZES, deliberately: upstream coverage at 16 px is far thinner than at 20, so the puller writes the
	// one available design to both rather than letting Microsoft serve the toolbar while the custom tree serves the
	// tree. Half a vendored name is the one outcome that changes a command's glyph between two places on screen.

	void the_icon_map_and_the_vendored_tree_agree ()
	{
		const QStringList mapped = vendored_names_from_the_map ();

		QVERIFY2 ( !mapped.isEmpty (), "no icon-map.json in the test resources, or it names nothing" );

		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			const QStringList vendored = base_names_in ( microsoft_directory ( designSize ), QStringLiteral ( ".svg" ) );

			for ( const QString& name : mapped )
			{
				QVERIFY2
				(
					vendored.contains ( name ),
					qPrintable ( QStringLiteral ( "icon-map names %1 but there is no vendored file at %2 px -- re-run "
					                              "tools/pull_microsoft_icons.py" ).arg ( name ).arg ( designSize ) )
				);
			}

			for ( const QString& name : vendored )
			{
				QVERIFY2
				(
					mapped.contains ( name ),
					qPrintable ( QStringLiteral ( "%1 is vendored at %2 px but the icon-map does not name it -- the "
					                              "puller's sweep would remove it" ).arg ( name ).arg ( designSize ) )
				);
			}
		}
	}

	// NO VENDORED FILE STATES A LITERAL COLOUR.
	//
	// Microsoft's regular set is filled #212121 at source, and the puller rewrites that one token to currentColor. Skip
	// the rewrite and nothing breaks, fails or warns: the glyph renders in near-black under BOTH themes, which is
	// correct-looking in Light and invisible-ish in Dark, and spec section 2.9's "icons recolour with the theme" is
	// quietly false for exactly the 32 names Microsoft serves.
	//
	// Asserted on the SOURCE rather than on a render, because a render says what one theme produced and this is a claim
	// about the file. The rendered half is a_fluent_glyph_takes_its_colour_from_the_palette, which now samples both
	// sub-sources.

	void no_vendored_file_states_a_literal_colour ()
	{
		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			const QDir directory ( microsoft_directory ( designSize ) );

			for ( const QString& fileName : directory.entryList ( { QStringLiteral ( "*.svg" ) }, QDir::Files ) )
			{
				QFile file ( directory.filePath ( fileName ) );

				QVERIFY2 ( file.open ( QIODevice::ReadOnly ), qPrintable ( fileName ) );

				const QString source = QString::fromUtf8 ( file.readAll () );

				QVERIFY2
				(
					source.contains ( QStringLiteral ( "currentColor" ) ),
					qPrintable ( QStringLiteral ( "vendored %1 at %2 px paints no currentColor -- it cannot be tinted" )
					             .arg ( fileName ).arg ( designSize ) )
				);

				// Every fill in the document must be the token or the "none" Microsoft puts on the <svg> root. A hex
				// literal anywhere is the un-normalised original.

				static const QRegularExpression FILL ( QStringLiteral ( "fill=\"([^\"]*)\"" ) );

				QRegularExpressionMatchIterator matches = FILL.globalMatch ( source );

				while ( matches.hasNext () )
				{
					const QString fill = matches.next ().captured ( 1 );

					QVERIFY2
					(
						fill == QStringLiteral ( "currentColor" ) || fill == QStringLiteral ( "none" ),
						qPrintable ( QStringLiteral ( "vendored %1 at %2 px states fill=\"%3\" -- it will ignore the "
						                              "palette" ).arg ( fileName ).arg ( designSize ).arg ( fill ) )
					);
				}
			}
		}
	}

	//=================================================================================================================
	// The ladder -- failures 2 and 3
	//=================================================================================================================

	// EVERY RUNG IS SERVED AT ITS OWN SIZE, IN BOTH MODES, IN BOTH FAMILIES.
	//
	// The size assertion is the one that matters and the one a weaker test misses: QIcon scales silently when it has
	// no exact entry, and actualSize reports the size ASKED FOR either way, so only reading back the delivered pixmap
	// can tell a real render from a resample.
	//
	// Both modes are checked because a partial mode table makes QIcon cross SIZES rather than modes -- it shipped, and
	// it made the 16 px artwork invisible while a command was disabled.

	void every_rung_is_served_at_its_own_size_in_both_modes ()
	{
		const InterfaceStyle families [] = { InterfaceStyle::Classic, InterfaceStyle::Fluent };

		const QIcon::Mode modes [] = { QIcon::Normal, QIcon::Disabled };

		for ( const InterfaceStyle family : families )
		{
			icons->set_interface_style ( family );

			for ( const QString& name : every_icon_name () )
			{
				const QIcon icon = icons->icon ( name );

				QVERIFY2 ( !icon.isNull (), qPrintable ( QStringLiteral ( "no icon for %1" ).arg ( name ) ) );

				for ( const config::icons::Rung& rung : config::icons::LADDER )
				{
					for ( const QIcon::Mode mode : modes )
					{
						const QPixmap pixmap = icon.pixmap ( QSize ( rung.deviceSize, rung.deviceSize ), mode, QIcon::Off );

						// A PENDING CLASSIC RUNG IS EXEMPT FROM THE SIZE ASSERTION AND FROM NOTHING ELSE. With no
						// drawn file at that rung, build_icon skips it and QIcon delivers a resample of the nearest
						// master, which is the documented behaviour while the artwork is outstanding. FLUENT IS NEVER
						// EXEMPT: both design SVGs exist, so every rung renders at its own size in that family
						// whatever Classic is missing -- and keeping Fluent asserted is what stops this becoming a
						// blanket skip for a name rather than for the family that actually lacks the file.

						const bool rungIsPending =
							( family == InterfaceStyle::Classic )
							&& pending_classic_rungs ().contains ( pending_key ( name, rung.deviceSize ) );

						if ( !rungIsPending )
						{
							QVERIFY2
							(
								pixmap.size () == QSize ( rung.deviceSize, rung.deviceSize ),
								qPrintable ( QStringLiteral ( "%1 at %2 px (%3) came back %4x%5 -- a scaled neighbour" )
								             .arg ( name ).arg ( rung.deviceSize )
								             .arg ( mode == QIcon::Normal ? "Normal" : "Disabled" )
								             .arg ( pixmap.width () ).arg ( pixmap.height () ) )
							);
						}

						// Asserted at EVERY rung including the pending ones: a resample is still artwork on screen,
						// so a pending rung that renders BLANK is a defect rather than a drawing nobody has got to.

						QVERIFY2
						(
							opaque_pixel_count ( pixmap.toImage () ) > 0,
							qPrintable ( QStringLiteral ( "%1 at %2 px rendered blank" ).arg ( name ).arg ( rung.deviceSize ) )
						);
					}
				}
			}
		}
	}

	// THE LADDER IS CHECKED AGAINST THE ASSET TREE, not against itself.
	//
	// Found by the D10 pass: deleting a rung from config::icons::LADDER failed NOTHING, because every case that walks
	// the ladder derives its expectations from the very table under test -- remove a rung and they simply check one
	// fewer. That is D15's shape inside a test rather than inside a neuter, and the fix is to compare the table with
	// something that did not come from it. The Classic directories are drawn by hand and are exactly the rungs, so
	// they are the independent witness; CMake already FATAL_ERRORs if one is missing.

	void the_ladder_covers_every_drawn_classic_size ()
	{
		const QDir root ( QStringLiteral ( ":/vje/icons/classic" ) );

		const QStringList drawn = root.entryList ( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );

		QVERIFY2 ( !drawn.isEmpty (), "no Classic size directories in the resource tree" );

		for ( const QString& entry : drawn )
		{
			bool       numeric    = false;
			const int  deviceSize = entry.toInt ( &numeric );

			QVERIFY2 ( numeric, qPrintable ( QStringLiteral ( "Classic directory %1 is not a size" ).arg ( entry ) ) );

			bool onTheLadder = false;

			for ( const config::icons::Rung& rung : config::icons::LADDER )
			{
				onTheLadder = onTheLadder || ( rung.deviceSize == deviceSize );
			}

			QVERIFY2
			(
				onTheLadder,
				qPrintable ( QStringLiteral ( "%1 px of Classic artwork is drawn and NOT on config::icons::LADDER -- "
				                              "nothing will ever ask for it" ).arg ( deviceSize ) )
			);
		}

		// And the other direction, so a rung naming a size nobody drew is caught too.

		for ( const config::icons::Rung& rung : config::icons::LADDER )
		{
			QVERIFY2
			(
				drawn.contains ( QString::number ( rung.deviceSize ) ),
				qPrintable ( QStringLiteral ( "LADDER names %1 px, which no Classic directory supplies" )
				             .arg ( rung.deviceSize ) )
			);
		}
	}

	//=================================================================================================================
	// Tinting -- failures 4 and 5, both checked in RENDERED PIXELS (lesson Q12)
	//=================================================================================================================

	// CLASSIC IS ARTWORK AND THE PALETTE DOES NOT TOUCH IT (SET-13a). Written as an EXACT claim rather than as "the two
	// renders differ": a render that changed with the theme in some other way would satisfy a difference test.

	void a_classic_glyph_is_drawn_exactly_as_authored ()
	{
		icons->set_interface_style ( InterfaceStyle::Classic );

		const int deviceSize = config::icons::LADDER [ 0 ].deviceSize;

		for ( const QString& name : every_icon_name () )
		{
			const QImage master ( classic_directory ( deviceSize ) + QLatin1Char ( '/' ) + name + QStringLiteral ( ".png" ) );

			QVERIFY ( !master.isNull () );

			theme->set_theme ( Theme::Light );
			theme->apply ();

			const QImage light = icons->icon ( name ).pixmap ( QSize ( deviceSize, deviceSize ) ).toImage ();

			theme->set_theme ( Theme::Dark );
			theme->apply ();

			const QImage dark = icons->icon ( name ).pixmap ( QSize ( deviceSize, deviceSize ) ).toImage ();

			// Identical across themes -- the "not tinted" half.

			QVERIFY2
			(
				light == dark,
				qPrintable ( QStringLiteral ( "Classic %1 changed with the theme -- it is being tinted" ).arg ( name ) )
			);

			// AND carrying the master's own colours -- the "exactly as authored" half. Both are needed: a build that
			// flattened every glyph to one colour would pass the first on its own.

			QCOMPARE ( opaque_colours ( light ), opaque_colours ( master ) );

			// The ANTIALIASING, which the colour comparison above deliberately no longer reaches (see opaque_colours:
			// a partly transparent pixel does not survive a QPixmap round trip bit-exactly). Alpha itself does, so the
			// shape of the artwork -- including every soft edge -- is pinned exactly, here, rather than not at all.

			QVERIFY2
			(
				alpha_channels_match ( light, master ),
				qPrintable ( QStringLiteral ( "Classic %1 is not delivered with the master's own alpha" ).arg ( name ) )
			);
		}

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	// FLUENT IS A MASK SET AND MUST TINT. Asserted against the palette's own colour rather than "the two renders
	// differ", so a build that recoloured wrongly could not pass.
	//
	// ONE SAMPLE FROM EACH SUB-SOURCE (Phase 15b.2). Fluent is now served by two trees whose glyphs arrive by different
	// routes -- the customs are traced with the token in place, the vendored ones have it substituted in by
	// tools/pull_microsoft_icons.py -- so a single sample answers for whichever tree happens to hold that name and says
	// nothing about the other. The samples are taken from the DIRECTORIES rather than named here, so this keeps
	// covering both as the partition moves.

	void a_fluent_glyph_takes_its_colour_from_the_palette ()
	{
		icons->set_interface_style ( InterfaceStyle::Fluent );

		const int designSize = config::icons::DESIGN_SIZES [ 0 ];

		const QStringList vendored = base_names_in ( microsoft_directory ( designSize ), QStringLiteral ( ".svg" ) );
		const QStringList custom   = base_names_in ( custom_directory    ( designSize ), QStringLiteral ( ".svg" ) );

		QStringList samples;

		if ( !vendored.isEmpty () ) samples << vendored.first ();
		if ( !custom.isEmpty ()   ) samples << custom.first ();

		QVERIFY2 ( !samples.isEmpty (), "neither Fluent sub-source holds a glyph" );

		const Theme themes [] = { Theme::Light, Theme::Dark };

		for ( const Theme scheme : themes )
		{
			theme->set_theme ( scheme );
			theme->apply ();

			const QColor expected = QApplication::palette ().color ( QPalette::Active, QPalette::WindowText );

			for ( const QString& name : samples )
			{
				const QImage rendered =
					icons->icon ( name ).pixmap ( QSize ( designSize, designSize ) ).toImage ();

				const QSet<QRgb> colours = opaque_colours ( rendered );

				QVERIFY2 ( !colours.isEmpty (), qPrintable ( QStringLiteral ( "%1 rendered blank" ).arg ( name ) ) );

				// Antialiasing means the edge pixels are the tint over transparency rather than the tint exactly, so the
				// claim is that the palette colour is PRESENT -- a glyph rendered in the wrong colour has none of it.

				QVERIFY2
				(
					colours.contains ( expected.rgb () ),
					qPrintable ( QStringLiteral ( "Fluent %1 carries no pixel of the palette's WindowText (%2)" )
					             .arg ( name, expected.name () ) )
				);
			}
		}

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	// The DISABLED form of a Classic glyph fades the artwork rather than draining it. Qt's own greying maps luminance
	// through a black-to-background-to-white ramp and strips every hue, which would discard the colour work exactly
	// where a command is unavailable -- measured at 65 hued pixels to 0 on document-save before this was changed.
	//
	// Scoped to a glyph that actually carries colour, and skipped with a message when none does yet, because the set
	// is being coloured and this case must not become a tripwire.

	void a_disabled_classic_glyph_keeps_its_hue ()
	{
		icons->set_interface_style ( InterfaceStyle::Classic );

		const int deviceSize = config::icons::LADDER [ 0 ].deviceSize;

		QString coloured;

		for ( const QString& name : every_icon_name () )
		{
			const QImage master ( classic_directory ( deviceSize ) + QLatin1Char ( '/' ) + name + QStringLiteral ( ".png" ) );

			if ( !master.isNull () && ( hued_pixel_count ( master ) > 0 ) )
			{
				coloured = name;

				break;
			}
		}

		if ( coloured.isEmpty () )
		{
			QSKIP ( "no Classic master carries colour yet -- nothing for a fade to preserve" );
		}

		const QIcon icon = icons->icon ( coloured );

		const QImage normal   = icon.pixmap ( QSize ( deviceSize, deviceSize ), QIcon::Normal,   QIcon::Off ).toImage ();
		const QImage disabled = icon.pixmap ( QSize ( deviceSize, deviceSize ), QIcon::Disabled, QIcon::Off ).toImage ();

		QVERIFY2
		(
			hued_pixel_count ( disabled ) > 0,
			qPrintable ( QStringLiteral ( "the disabled form of %1 has no hue left -- it is being drained, not faded" )
			             .arg ( coloured ) )
		);

		// AND IT IS MEASURABLY DIMMER. "disabled != normal" was the first version of this and the D10 pass showed it
		// passing against DISABLED_ARTWORK_OPACITY = 1.00 -- the two renders still differ by a pixel or two, because
		// compositing through a premultiplied buffer is not bit-identical to a straight format conversion. A
		// difference test was therefore satisfied by rounding noise. The claim is a QUANTITY: summed alpha must drop
		// by materially more than the dial's own margin of error.

		QVERIFY2 ( opaque_pixel_count ( disabled ) > 0, "the disabled form rendered blank" );

		const double normalAlpha   = static_cast<double> ( summed_alpha ( normal ) );
		const double disabledAlpha = static_cast<double> ( summed_alpha ( disabled ) );

		QVERIFY2 ( normalAlpha > 0.0, "the normal form rendered blank" );

		const double retained = disabledAlpha / normalAlpha;

		QVERIFY2
		(
			retained < 0.75,
			qPrintable ( QStringLiteral ( "the disabled form retains %1 of the artwork's alpha -- it is not fading" )
			             .arg ( retained, 0, 'f', 2 ) )
		);
	}

	// The colour partition, REPORTED and never asserted. The Classic set is being coloured glyph by glyph and size by
	// size, so any number here would be a tripwire; what this leaves behind is a record in the test log of how far the
	// work has got, which is the thing a maintainer actually wants to know.

	void the_classic_colour_partition_is_reported ()
	{
		for ( const config::icons::Rung& rung : config::icons::LADDER )
		{
			int coloured = 0;
			int present  = 0;

			for ( const QString& name : every_icon_name () )
			{
				const QImage master ( classic_directory ( rung.deviceSize ) + QLatin1Char ( '/' ) + name + QStringLiteral ( ".png" ) );

				if ( master.isNull () )
				{
					continue;
				}

				++present;

				if ( opaque_colours ( master ).size () > 1 )
				{
					++coloured;
				}
			}

			qInfo ().noquote () << QStringLiteral ( "Classic %1 px: %2 of %3 masters carry more than one colour" )
			                           .arg ( rung.deviceSize ).arg ( coloured ).arg ( present );
		}
	}

	//=================================================================================================================
	// The one derived asset -- failure 6
	//=================================================================================================================

	// THE CUSTOM SVGs MATCH THE REFERENCE PNGs THEY WERE TRACED FROM. This is what the retired
	// the_two_trees_are_the_same_artwork case becomes: not "the two shipped trees agree", which is no longer wanted,
	// but "the generated tree agrees with its input", which is the only derivation left.
	//
	// Compared on ALPHA, because that is the whole of a traced glyph's shape: the reference is one ink colour and the
	// SVG paints in currentColor, so the colours are not comparable and were never the claim.

	void the_custom_svgs_match_their_reference_pngs ()
	{
		// A DERIVATION CAN ONLY GO STALE WHERE IT IS RUN, so this case belongs to the tree the tracer runs in. The
		// published tree carries the SVGs and not their inputs, and there is nothing there that could drift: the
		// SVGs arrived finished. Skipping is therefore the honest answer rather than a weakened check -- and no
		// coverage is lost, because `tools/verify.py` runs `trace_png_to_svg.py --check` against the PRIVATE tree
		// before every promotion, which is the same claim asked at the scope that can answer it.

		if ( !reference_tree_is_present () )
		{
			QSKIP ( "no reference PNGs in this tree -- the tracer's inputs are private, and the drift check runs "
			        "against the private tree in tools/verify.py" );
		}

		for ( const int designSize : config::icons::DESIGN_SIZES )
		{
			for ( const QString& name : base_names_in ( custom_directory ( designSize ), QStringLiteral ( ".svg" ) ) )
			{
				const QImage reference ( reference_png_path ( name, designSize ) );

				QVERIFY2
				(
					!reference.isNull (),
					qPrintable ( QStringLiteral ( "custom %1 at %2 px has no reference PNG -- it was traced from "
					                              "something that is no longer there" ).arg ( name ).arg ( designSize ) )
				);

				icons->set_interface_style ( InterfaceStyle::Fluent );

				const QImage rendered =
					icons->icon ( name ).pixmap ( QSize ( designSize, designSize ) ).toImage ();

				QVERIFY2
				(
					alpha_channels_match ( rendered, reference ),
					qPrintable ( QStringLiteral ( "custom %1 at %2 px differs from its reference PNG -- re-run "
					                              "tools/trace_png_to_svg.py" ).arg ( name ).arg ( designSize ) )
				);
			}
		}
	}

	//=================================================================================================================
	// The family switch
	//=================================================================================================================

	// Written BOTH WAYS deliberately, so neither direction passes against a library that ignores the setter.

	void switching_the_family_changes_the_pixels ()
	{
		// ANY name serves. An earlier version picked one whose Classic master was monochrome at every rung, on the
		// idea that it made the comparison cleaner -- it made the case SKIP instead, the day the 16 set finished being
		// coloured, which is the one outcome a regression must not have. The two families are different artwork by
		// construction; there is nothing to be careful about.

		const QString name = icon_names::DOCUMENT_OPEN;

		const int size = config::icons::DESIGN_SIZES [ 0 ];

		icons->set_interface_style ( InterfaceStyle::Classic );

		const QImage classic = icons->icon ( name ).pixmap ( QSize ( size, size ) ).toImage ();

		icons->set_interface_style ( InterfaceStyle::Fluent );

		const QImage fluent = icons->icon ( name ).pixmap ( QSize ( size, size ) ).toImage ();

		QVERIFY2 ( classic != fluent, "Classic and Fluent rendered identically -- the family was ignored" );

		icons->set_interface_style ( InterfaceStyle::Classic );

		const QImage back = icons->icon ( name ).pixmap ( QSize ( size, size ) ).toImage ();

		QVERIFY2 ( back == classic, "switching back did not restore the Classic artwork" );
	}

	void setting_the_same_family_is_a_no_op ()
	{
		icons->set_interface_style ( InterfaceStyle::Classic );

		QSignalSpy spy ( icons.get (), &IconLibrary::icons_changed );

		icons->set_interface_style ( InterfaceStyle::Classic );

		QCOMPARE ( spy.count (), 0 );
	}

	void switching_the_family_announces_itself ()
	{
		icons->set_interface_style ( InterfaceStyle::Classic );

		QSignalSpy spy ( icons.get (), &IconLibrary::icons_changed );

		icons->set_interface_style ( InterfaceStyle::Fluent );

		QVERIFY ( spy.count () >= 1 );
	}

	//=================================================================================================================
	// The surface
	//=================================================================================================================

	void available_icons_reports_each_family ()
	{
		const QStringList expected = [] { QStringList names = every_icon_name (); names.sort (); return names; } ();

		const InterfaceStyle families [] = { InterfaceStyle::Classic, InterfaceStyle::Fluent };

		for ( const InterfaceStyle family : families )
		{
			const QStringList reported = IconLibrary::available_icons ( family );

			for ( const QString& name : expected )
			{
				QVERIFY2 ( reported.contains ( name ), qPrintable ( QStringLiteral ( "%1 unreported" ).arg ( name ) ) );
			}

			// De-duplicated: a name held by both Fluent sub-sources is one glyph, and reporting it twice reads as a
			// missing file rather than as a double count.

			QCOMPARE ( reported.size (), QSet<QString> ( reported.begin (), reported.end () ).size () );
		}
	}

	void has_icon_reports_presence_in_the_current_family ()
	{
		const InterfaceStyle families [] = { InterfaceStyle::Classic, InterfaceStyle::Fluent };

		for ( const InterfaceStyle family : families )
		{
			icons->set_interface_style ( family );

			QVERIFY ( icons->has_icon ( icon_names::DOCUMENT_OPEN ) );
			QVERIFY ( !icons->has_icon ( QStringLiteral ( "no-such-icon" ) ) );
		}
	}

	void unknown_name_returns_a_null_icon ()
	{
		QVERIFY ( icons->icon ( QStringLiteral ( "no-such-icon" ) ).isNull () );
	}

	//=================================================================================================================
	// Caching
	//=================================================================================================================

	void applying_a_theme_emits_icons_changed ()
	{
		QSignalSpy spy ( icons.get (), &IconLibrary::icons_changed );

		theme->set_theme ( Theme::Dark );
		theme->apply ();

		QVERIFY ( spy.count () >= 1 );

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	void repeated_requests_return_the_cached_icon ()
	{
		QCOMPARE ( icons->icon ( icon_names::DOCUMENT_SAVE ).cacheKey (),
		           icons->icon ( icon_names::DOCUMENT_SAVE ).cacheKey () );
	}

	void cache_is_dropped_on_a_theme_change ()
	{
		// Fluent, because Classic is deliberately theme-independent -- its pixels are identical either way, so a cache
		// drop is unobservable there. Asking the family that DOES answer the palette is what makes this assertable.

		icons->set_interface_style ( InterfaceStyle::Fluent );

		const qint64 before = icons->icon ( icon_names::DOCUMENT_SAVE ).cacheKey ();

		theme->set_theme ( Theme::Dark );
		theme->apply ();

		QVERIFY ( icons->icon ( icon_names::DOCUMENT_SAVE ).cacheKey () != before );

		theme->set_theme ( Theme::Light );
		theme->apply ();
	}

	//=================================================================================================================
	// Data Members
	//=================================================================================================================

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;
	std::unique_ptr<IconLibrary>   icons;
};

QTEST_MAIN ( TestIconLibrary )

#include "tst_icon_library.moc"

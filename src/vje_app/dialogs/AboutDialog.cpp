//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   AboutDialog implementation. See the header for why About is a dialog while the prompts stay message boxes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/AboutDialog.hpp"

#include "dialogs/dialog_frame.hpp"

#include "AppConfig.hpp"

#include <vje_core/version.hpp>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSize>

namespace vje
{
	namespace
	{
		// The project page HELP-04 asks for. Stated here rather than in AppConfig, whose own rule excludes
		// single-call-site values -- this is one, and it is not a tunable.

		const QString PROJECT_URL = QStringLiteral ( "https://github.com/rohingosling/vje" );

		// THE THIRD-PARTY NOTICE (Phase 15b.2). Part of the Fluent icon family is vendored from Microsoft's Fluent
		// System Icons under MIT, which requires the notice to travel with the redistribution -- so it is in the
		// application a user can actually open, not only in the repository's LICENSE files.
		//
		// Named here beside the project URL for the same reason: single call site, not a tunable.

		const QString FLUENT_ICONS_URL = QStringLiteral ( "https://github.com/microsoft/fluentui-system-icons" );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	AboutDialog::AboutDialog ( const QIcon& icon, const QIcon& applicationIcon, QWidget* parent )
	:	QDialog ( parent )
	{
		setWindowTitle ( tr ( "About VJE" ) );

		// The product's mark, at an AUTHORED size (see config::dialog::ABOUT_ICON_SIZE). The application icon is
		// hand-drawn raster, so asking for a size nobody drew would put a resampled logo in the one dialog that exists
		// to present the product.

		iconLabel = new QLabel ( this );

		iconLabel->setPixmap ( applicationIcon.pixmap ( QSize ( config::dialog::ABOUT_ICON_SIZE,
		                                                        config::dialog::ABOUT_ICON_SIZE ) ) );

		iconLabel->setFixedSize ( config::dialog::ABOUT_ICON_SIZE, config::dialog::ABOUT_ICON_SIZE );

		// DELIBERATELY UNNAMED for NFR-05. It is decorative: the text beside it already says "VJE -- Versatile JSON
		// Editor", so an accessible name here would make a screen reader announce the product twice. Recorded rather
		// than left blank, because the Phase 14 sweep's rule is that every control carries a name and this is the
		// exception to it.

		contentLabel = new QLabel ( this );

		// Rich text, because HELP-04 asks for a LINK and the product name carries an expansion whose initials are the
		// product name -- both of which are markup rather than layout.

		contentLabel->setTextFormat ( Qt::RichText );
		contentLabel->setOpenExternalLinks ( true );
		contentLabel->setTextInteractionFlags ( Qt::TextBrowserInteraction );
		contentLabel->setWordWrap ( true );

		contentLabel->setText
		(
			tr
			(
				"<p><b>VJE</b> \xE2\x80\x94 <b>V</b>ersatile <b>J</b>SON <b>E</b>ditor</p>"
				"<p>Version %1<br>"
				"\xC2\xA9 Rohin Gosling</p>"
				"<p><a href=\"%2\">%2</a></p>"
				"<p>Part of the Fluent icon set is from Microsoft\xE2\x80\x99s "
				"<a href=\"%3\">Fluent System Icons</a>, \xC2\xA9 2020 Microsoft Corporation, "
				"used under the MIT licence.</p>"
			)
			.arg ( version_string (), PROJECT_URL, FLUENT_ICONS_URL )
		);

		// NFR-05. The label IS the dialog's content, and a screen reader reaching it by tab (it is focusable, because
		// the link has to be) should be told what it has landed on rather than reading the markup cold.

		contentLabel->setAccessibleName ( tr ( "About VJE" ) );

		QWidget* const content = new QWidget ( this );

		// Icon left, text right, both aligned to the TOP -- the icon is a fixed 40 while the text grows with the
		// version string and the third-party notice, so centring would float the mark against a block that is taller
		// than it is. The same rule the wrapped Form View arrived at (spec section 2.10).

		QHBoxLayout* const contentLayout = new QHBoxLayout ( content );

		contentLayout->setContentsMargins ( 0, 0, 0, 0 );
		contentLayout->setSpacing ( config::dialog::ABOUT_ICON_SPACING );

		contentLayout->addWidget ( iconLabel,    0, Qt::AlignTop );
		contentLayout->addWidget ( contentLabel, 1, Qt::AlignTop );

		// Close rather than OK: there is nothing here to accept, and a button reading OK invites the reader to wonder
		// what they just agreed to.

		QDialogButtonBox* const buttons = new QDialogButtonBox ( QDialogButtonBox::Close, this );

		connect ( buttons, &QDialogButtonBox::rejected, this, &AboutDialog::reject );

		apply_dialog_frame ( *this, content, buttons, icon );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QLabel* AboutDialog::content_label () const
	{
		return contentLabel;
	}

	QLabel* AboutDialog::icon_label () const
	{
		return iconLabel;
	}
}

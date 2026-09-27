//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   text_prompt -- a one-line text prompt (a label, a field, OK and Cancel) opened at config::text_prompt::WIDTH: the
//   key the Add commands ask for, and Rename Key.
//
//   A QInputDialog, sized by RESIZING IT. Qt's static QInputDialog::getText cannot be given a size at all, and on a
//   QInputDialog of its own setFixedWidth, setMinimumWidth and setMaximumWidth are all undone: its layout's size
//   constraint is SetMinAndMaxSize, which resets the dialog's limits from the layout when it is shown -- measured (Qt
//   6.10.1): each opened at the natural 200 px. A resize to the width before it is shown holds.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "AppConfig.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace vje
{
	// The text entered, or none when the prompt was cancelled -- distinct from the EMPTY string, which is a legal JSON
	// member key and has to be expressible.
	//
	// width is the width the prompt opens at: config::text_prompt::WIDTH unless a caller says otherwise, or none for
	// the width Qt's layout gives it (Rename Column, which keeps it). Every prompt takes the dialog inset and the rule
	// above the buttons whatever its width (STYLE-15).

	std::optional<QString> ask_text_prompt
	(
		QWidget*           parent,
		const QString&     title,
		const QString&     label,
		const QString&     initialValue,
		std::optional<int> width = config::text_prompt::WIDTH
	);
}

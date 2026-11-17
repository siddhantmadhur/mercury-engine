// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// mercury_editor [path/to/project.mproj] [--map res://maps/name.mmap]
//
// Opens the given project (at the given map, or its main map), or the
// project launcher when none is given.

#include <wx/wx.h>

#include "editor_app.h"

wxIMPLEMENT_APP(Mercury::EditorUI::MercuryEditorApp);

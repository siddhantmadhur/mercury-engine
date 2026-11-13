// mercury_editor [path/to/project.mproj] [--map res://maps/name.mmap]
//
// Opens the given project (at the given map, or its main map), or the
// project launcher when none is given.

#include <wx/wx.h>

#include "editor_app.h"

wxIMPLEMENT_APP(Mercury::EditorUI::MercuryEditorApp);

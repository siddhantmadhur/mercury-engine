// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURY_EXAMPLES_CPP_HELLO_SYSTEM_HELLO_PANEL_H
#define MERCURY_EXAMPLES_CPP_HELLO_SYSTEM_HELLO_PANEL_H

#include <wx/wx.h>

namespace Mercury {
class EditorScene;
}

namespace Mercury::EditorUI {

// A dockable editor panel listing the nodes of the open map. It is created by
// the Hello system module (hello_panel.cc).
class HelloPanel : public wxPanel {
 public:
  HelloPanel(wxWindow* parent, EditorScene& scene);

  void RefreshEntities();

 private:
  EditorScene& scene_;
  wxListBox* list_ = nullptr;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EXAMPLES_CPP_HELLO_SYSTEM_HELLO_PANEL_H

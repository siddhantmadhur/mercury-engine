/**
 * panel.hh
 *
 * simple wrapper around ImGui to create specific panels for the editor
 **/

#ifndef PEQUOD_EDITOR_PANEL_IMPL_HH_
#define PEQUOD_EDITOR_PANEL_IMPL_HH_

namespace Pequod {
class Panel {
 public:
  virtual ~Panel() = default;

  Panel(bool init_show = false);

  virtual void Initialize() = 0;

  virtual void Draw() = 0;

  void Hide();

  void Show();

  void Toggle();

  bool IsShown();

 protected:
  bool show_panel;
};
}  // namespace Pequod

#endif

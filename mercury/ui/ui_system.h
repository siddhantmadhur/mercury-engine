/*
 * ui_system.h
 *
 * Runs every UI panel of a game scene: feeds them input from the
 * InputManager, updates them each frame and merges what they draw. Owned by
 * MapScene; UIDocument components add their trees to it.
 */
#ifndef MERCURY_UI_UI_SYSTEM_H
#define MERCURY_UI_UI_SYSTEM_H

#include <memory>
#include <string>
#include <vector>

#include "ui/panel.h"
#include "ui/uxml.h"

namespace Mercury {
class InputManager;
}

namespace Mercury::UI {

class UISystem {
 public:
  UISystem();
  ~UISystem();

  UIResources& resources() { return resources_; }

  // The panel for a PanelSettings asset ("" for the defaults), made on
  // first use.
  Panel* GetPanel(const std::string& settings_path, const PanelSettings& settings);

  /**
   * @brief Shows a document in a panel
   * @return its root, a TemplateContainer that fills the panel
   */
  VisualElement* AddDocument(Panel* panel, const std::shared_ptr<VisualTreeAsset>& tree,
                             float sort_order);
  void RemoveDocument(VisualElement* root);
  // Removes every panel and document.
  void Clear();

  // Turns this frame's input into UI events for the panels.
  void ProcessInput(const InputManager& input);
  void Update(double dt_seconds, glm::vec2 screen_size, float dpi_scale);

  // Every panel's triangles, bottom panel first.
  const UIDrawData& GetDrawData() const { return draw_data_; }
  bool empty() const { return panels_.empty(); }

  // True while the pointer is over an element that blocks input, so the
  // game can ignore clicks meant for the UI.
  bool IsPointerOverUI() const;
  // True while a text field has focus.
  bool WantsKeyboard() const;

  const std::vector<std::unique_ptr<Panel>>& panels() const { return panel_list_; }

 private:
  struct PanelEntry {
    std::string settings_path;
    Panel* panel;
  };
  struct DocumentEntry {
    VisualElement* root;
    Panel* panel;
    float sort_order;
  };
  std::vector<Panel*> PanelsTopFirst() const;

  UIResources resources_;
  std::vector<std::unique_ptr<Panel>> panel_list_;
  std::vector<PanelEntry> panels_;
  std::vector<DocumentEntry> documents_;
  UIDrawData draw_data_;
  Panel* keyboard_panel_ = nullptr;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_UI_SYSTEM_H

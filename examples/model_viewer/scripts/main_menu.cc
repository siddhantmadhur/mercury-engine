#include "main_menu.h"

#include "ui/controls.h"

MERCURY_SCRIPT(MainMenu);

using namespace Mercury::UI;

namespace {

// Buttons in ui/menu.uxml; each opens maps/<name>.mmap.
constexpr const char* kScenes[] = {
    "sponza",         "sibenik",         "conference",     "breakfast_room",
    "living_room",    "gallery",         "bunny",          "mori_knob",
    "cornell_original", "cornell_sphere", "cornell_water", "cornell_glossy",
    "cornell_mirror",
};

}  // namespace

void MainMenu::OnStart() {
  auto* document = GetDocument();
  VisualElement* root = document ? document->rootVisualElement() : nullptr;
  if (!root) return;
  for (const char* scene : kScenes) {
    auto* button = root->Q<Button>(scene);
    if (!button) continue;
    const std::string name = scene;
    const std::string title = button->text();
    button->clicked += [this, name, title] { Open(name, title); };
  }
  if (auto* quit = root->Q<Button>("quit")) quit->clicked += [this] { Quit(); };
}

void MainMenu::Open(const std::string& map, const std::string& title) {
  if (!pending_.empty()) return;
  pending_ = map;
  // Big scenes take a few seconds to load: say so before the frame stalls.
  wait_frames_ = 2;
  if (VisualElement* root = GetDocument()->rootVisualElement()) {
    root->Q<Label>("loading")->SetText("Loading " + title + "…");
    root->Q("loading-overlay")->EnableInClassList("hidden", false);
  }
}

void MainMenu::OnFrame(double dt) {
  if (pending_.empty() || --wait_frames_ > 0) return;
  LoadMap("res://maps/" + pending_ + ".mmap");
}

void MainMenu::OnTick(double dt) {
  if (IsKeyJustPressed(GLFW_KEY_ESCAPE)) Quit();
}

#include "scene_hud.h"

#include <format>

#include "flythrough_camera.h"
#include "ui/controls.h"

MERCURY_SCRIPT(SceneHud);

using namespace Mercury::UI;

void SceneHud::ReflectData(Mercury::Reflector& r) {
  r.Field("title", title);
  r.Field("hint", hint);
  r.Field("fps_text", fps_text_);
  r.Property("lap_text", [this] {
    auto* flythrough = Get(camera).GetBehaviour<FlythroughCamera>();
    if (!flythrough) return Mercury::mtext::Variant(std::string());
    std::string text = std::format("Lap {}  {:.0f}%", flythrough->Laps() + 1,
                                   flythrough->LapProgress() * 100.0f);
    if (!flythrough->LastLapSummary().empty()) {
      text += "\nLast lap: " + flythrough->LastLapSummary();
    }
    return Mercury::mtext::Variant(text);
  });
}

void SceneHud::OnStart() {
  auto* document = GetDocument();
  VisualElement* root = document ? document->rootVisualElement() : nullptr;
  if (!root) return;
  root->SetDataSource(AsDataSource());
  if (auto* back = root->Q<Button>("back")) back->clicked += [this] { BackToMenu(); };
}

void SceneHud::OnFrame(double dt) {
  frames_++;
  elapsed_ += static_cast<float>(dt);
  if (elapsed_ >= 0.5f) {
    fps_text_ = std::format("{:.0f} fps  ({:.2f} ms)", frames_ / elapsed_,
                            1000.0f * elapsed_ / frames_);
    frames_ = 0;
    elapsed_ = 0.0f;
  }
}

void SceneHud::OnTick(double dt) {
  if (IsKeyJustPressed(GLFW_KEY_ESCAPE)) BackToMenu();
}

void SceneHud::BackToMenu() { LoadMap("res://maps/menu.mmap"); }

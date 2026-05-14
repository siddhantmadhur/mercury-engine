#include <memory>

#include "application/default_application.h"
#include "application/default_application.h"
#include "os/filesystem.h"
#include "ui_editor/scene.h"

int main(void) {
  auto application =
      Pequod::DefaultApplication("Pequod Editor [alpha]", 1920, 1080);
  auto entry_scene = std::make_unique<Pequod::UIEditorScene>();
  application.SetGameScene(std::move(entry_scene));
  // application.SetPointer(path.string());
  return application.Run();
}

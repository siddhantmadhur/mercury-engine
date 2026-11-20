// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "templates.h"

#include <cctype>
#include <format>
#include <fstream>

#include "input/input_actions.h"
#include "os/filesystem.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "project_scaffold.h"
#include "scripting/type_registry.h"

namespace Mercury::Templates {

namespace fs = std::filesystem;

namespace {

bool WriteText(const fs::path& file, const std::string& text,
               std::string* error) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  // Atomic and symlink-safe: a link planted where the file goes (in a cloned
  // project) is replaced, not written through to somewhere else.
  if (!os::WriteFileAtomic(file, text)) {
    if (error) *error = "could not write " + file.string();
    return false;
  }
  return true;
}

std::string ToTargetSlug(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else if (!out.empty() && out.back() != '_') {
      out.push_back('_');
    }
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
    out = "game_" + out;
  }
  return out;
}

}  // namespace

const char* ScriptKindName(ScriptKind kind) {
  switch (kind) {
    case ScriptKind::kAngelScript:
      return "angel_script";
    case ScriptKind::kCppBehaviour:
      return "cpp_behaviour";
    case ScriptKind::kCppSystem:
      return "cpp_system";
    case ScriptKind::kCppEditorSystem:
      return "cpp_editor_system";
  }
  return "angel_script";
}

ScriptKind ScriptKindFromName(std::string_view name) {
  if (name == "cpp_behaviour") return ScriptKind::kCppBehaviour;
  if (name == "cpp_system") return ScriptKind::kCppSystem;
  if (name == "cpp_editor_system") return ScriptKind::kCppEditorSystem;
  return ScriptKind::kAngelScript;
}

bool IsCppScript(ScriptKind kind) { return kind != ScriptKind::kAngelScript; }

bool IsEditorSystem(ScriptKind kind) {
  return kind == ScriptKind::kCppEditorSystem;
}

ScriptPaths GetScriptPaths(const fs::path& project_root, ScriptKind kind,
                           const std::string& class_name) {
  const std::string snake = ToSnakeCase(class_name);
  ScriptPaths paths;
  if (kind == ScriptKind::kAngelScript) {
    paths.source = project_root / "scripts" / (snake + ".as");
    return paths;
  }
  const fs::path dir =
      project_root / (kind == ScriptKind::kCppEditorSystem ? "system" : "src");
  paths.header = dir / (snake + ".h");
  paths.source = dir / (snake + ".cc");
  return paths;
}

bool IsValidIdentifier(const std::string& name) {
  if (name.empty()) return false;
  if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') {
    return false;
  }
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

std::string ToSnakeCase(const std::string& name) {
  std::string out;
  for (size_t i = 0; i < name.size(); i++) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    if (std::isupper(c)) {
      const bool prev_lower =
          i > 0 && (std::islower(static_cast<unsigned char>(name[i - 1])) ||
                    std::isdigit(static_cast<unsigned char>(name[i - 1])));
      const bool next_lower =
          i + 1 < name.size() &&
          std::islower(static_cast<unsigned char>(name[i + 1]));
      if (!out.empty() && out.back() != '_' &&
          (prev_lower || (next_lower && i > 0))) {
        out.push_back('_');
      }
      out.push_back(static_cast<char>(std::tolower(c)));
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

std::string ToTargetName(const std::string& name) { return ToTargetSlug(name); }

std::string NewMapText(const std::string& root_name, bool is_3d) {
  Map map;
  map.RenameNode(map.GetRoot(), root_name);
  map.GetRoot()->components.push_back({"Transform", {}});

  auto add_node = [&](const char* name, glm::vec3 position,
                      glm::vec3 rotation = glm::vec3(0.0f),
                      glm::vec3 scale = glm::vec3(1.0f)) {
    MapNode* node = map.AddNode(map.GetRoot(), name);
    ComponentData transform{"Transform", {}};
    if (position != glm::vec3(0.0f))
      transform.properties.Set("position", position);
    if (rotation != glm::vec3(0.0f))
      transform.properties.Set("rotation", rotation);
    if (scale != glm::vec3(1.0f)) transform.properties.Set("scale", scale);
    node->components.push_back(std::move(transform));
    return node;
  };

  if (!is_3d) {
    MapNode* camera = add_node("Camera", glm::vec3(0.0f));
    ComponentData settings{"Camera", {}};
    settings.properties.Set("fit_height", 90.0);
    camera->components.push_back(std::move(settings));
    return map.SaveToString();
  }

  add_node("Camera", {0, 3, 8}, {-15, 0, 0})
      ->components.push_back({"Camera", {}});
  add_node("Sun", {0, 6, 0}, {-50, -30, 0})
      ->components.push_back({"DirectionalLight", {}});

  MapNode* ground =
      add_node("Ground", glm::vec3(0.0f), glm::vec3(0.0f), {20, 1, 20});
  ComponentData plane{"MeshRenderer", {}};
  plane.properties.Set("mesh", std::string("plane"));
  plane.properties.Set("color",
                       mtext::Color{glm::vec4(0.55f, 0.58f, 0.62f, 1.0f)});
  ground->components.push_back(std::move(plane));
  ComponentData ground_collider{"BoxCollider", {}};
  ground_collider.properties.Set("size", glm::vec3(1.0f, 0.01f, 1.0f));
  ground->components.push_back(std::move(ground_collider));

  MapNode* cube = add_node("Cube", {0, 0.5f, 0});
  cube->components.push_back({"MeshRenderer", {}});
  cube->components.push_back({"BoxCollider", {}});
  return map.SaveToString();
}

fs::path CreateProject(const fs::path& parent, const std::string& name,
                       bool is_3d, std::string* error) {
  if (name.empty()) {
    if (error) *error = "the project needs a name";
    return {};
  }
  const fs::path root = parent / name;
  std::error_code ec;
  if (fs::exists(root, ec) && !fs::is_empty(root, ec)) {
    if (error) *error = root.string() + " already exists and is not empty";
    return {};
  }
  fs::create_directories(root / "scripts", ec);
  fs::create_directories(root / "src", ec);
  fs::create_directories(root / "system", ec);
  fs::create_directories(root / "assets", ec);
  fs::create_directories(root / "maps", ec);
  if (ec) {
    if (error) *error = "could not create " + root.string();
    return {};
  }

  const fs::path mproj =
      root / (ToTargetName(name) + std::string(kProjectExtension));
  auto project = Project::Create(mproj, name);
  if (!project->Save()) {
    if (error) *error = "could not write " + mproj.string();
    return {};
  }

  // Build output and editor caches are generated, so keep them out of git.
  // .clangd tracks the editor that last opened the project, and the compile
  // database link is regenerated after each configure.
  if (!WriteText(root / ".gitignore",
                 "# Build output and editor caches\n"
                 ".mercury/\n"
                 ".clangd\n"
                 "compile_commands.json\n",
                 error)) {
    return {};
  }
  EditorUI::ScaffoldProject(*project);
  if (!WriteText(root / "maps" / "main.mmap", NewMapText(name, is_3d), error)) {
    return {};
  }
  if (!CreateAsset(root / "assets" / "default.masset", "Material")) {
    if (error) *error = "could not write the default material";
    return {};
  }
  return mproj;
}

namespace {

std::string CppBehaviourHeader(const std::string& name) {
  return std::format(
      R"(#pragma once

#include <scripting/behaviour.h>

// A C++ gameplay component. MERCURY_SCRIPT in the .cc registers it; the
// editor lists it under Add Component after the project builds.
class {0} : public Mercury::Behaviour {{
 public:
  void OnStart() override;
  void OnFrame(double dt) override;

  // Fields the Inspector shows and the map saves (see scripting/reflection.h):
  // float speed_ = 1.0f;
  //
  // void Reflect(Mercury::Reflector& r) override {{
  //   r.Field("speed", speed_).Tooltip("World units per second");
  // }}
}};
)",
      name);
}

std::string CppBehaviourSource(const std::string& name,
                               const std::string& snake) {
  return std::format(
      R"(#include "{1}.h"

// Makes the class available to maps as [component type="{0}"].
MERCURY_SCRIPT({0});

void {0}::OnStart() {{}}

void {0}::OnFrame(double dt) {{
  (void)dt;
}}
)",
      name, snake);
}

std::string CppSystemHeader(const std::string& name) {
  return std::format(
      R"(#pragma once

#include <scripting/system.h>

// A scene-wide subsystem: one instance per scene, created when the scene
// starts. MERCURY_SYSTEM in the .cc registers it.
class {0} : public Mercury::System {{
 public:
  // Network receive runs before the behaviours; network send after physics:
  // Phase GetPhase() const override {{ return Phase::kPostPhysics; }}

  void OnRegister(Mercury::MapScene& scene) override;
  void OnTick(Mercury::MapScene& scene, double dt) override;
}};
)",
      name);
}

std::string CppSystemSource(const std::string& name, const std::string& snake) {
  return std::format(
      R"(#include "{1}.h"

// Makes the system available to every scene.
MERCURY_SYSTEM({0});

void {0}::OnRegister(Mercury::MapScene& scene) {{
  (void)scene;
}}

void {0}::OnTick(Mercury::MapScene& scene, double dt) {{
  (void)scene;
  (void)dt;
}}
)",
      name, snake);
}

std::string CppEditorSystemHeader(const std::string& name) {
  return std::format(
      R"(#pragma once

#include <extension.h>

// The editor loads this module while the project is open. Everything it
// registers through EditorContext is removed when the module unloads.
class {0} : public Mercury::EditorUI::EditorExtension {{
 public:
  void OnLoad(Mercury::EditorUI::EditorContext& context) override;
}};
)",
      name);
}

std::string CppEditorSystemSource(const std::string& name,
                                  const std::string& snake) {
  return std::format(
      R"(#include "{1}.h"

#include <console.h>

// Makes the editor create this extension from the system/ module.
MERCURY_EDITOR_EXTENSION({0});

void {0}::OnLoad(Mercury::EditorUI::EditorContext& context) {{
  auto* console = &context.GetConsole();
  // Register UI through the context, never by binding wx handlers directly:
  // the context removes this menu item while the module is still mapped.
  context.AddMenuItem({{.menu = "Extensions",
                        .label = "{0}",
                        .callback = [console]() {{
                          console->Info("Hello from {0}");
                        }}}});
}}
)",
      name, snake);
}

}  // namespace

fs::path CreateScript(const fs::path& project_root,
                      const std::string& class_name, std::string* error) {
  return CreateScript(project_root, class_name, ScriptKind::kAngelScript,
                      error);
}

fs::path CreateScript(const fs::path& project_root,
                      const std::string& class_name, ScriptKind kind,
                      std::string* error) {
  if (!IsValidIdentifier(class_name)) {
    if (error) {
      *error = "\"" + class_name + "\" is not a valid " +
               (IsCppScript(kind) ? "C++" : "AngelScript") + " class name";
    }
    return {};
  }

  const ScriptPaths paths = GetScriptPaths(project_root, kind, class_name);
  if (kind == ScriptKind::kAngelScript) {
    std::error_code ec;
    if (fs::symlink_status(paths.source, ec).type() !=
        fs::file_type::not_found) {
      if (error) *error = paths.source.filename().string() + " already exists";
      return {};
    }
    const std::string text = std::format(
        R"([script]
class {0} : Behaviour
{{
    [field tooltip="Shown in the Inspector and saved in the map."]
    float speed = 1.0f;

    void onStart() {{}}
    void onTick(double dt) {{}}
}}
)",
        class_name);
    if (!WriteText(paths.source, text, error)) return {};
    return paths.source;
  }

  // Never overwrite: a cloned project could contain either file already.
  std::error_code ec;
  for (const fs::path& file : {paths.header, paths.source}) {
    if (fs::symlink_status(file, ec).type() != fs::file_type::not_found) {
      if (error) *error = file.filename().string() + " already exists";
      return {};
    }
  }

  const std::string snake = ToSnakeCase(class_name);
  std::string header;
  std::string source;
  switch (kind) {
    case ScriptKind::kCppBehaviour:
      header = CppBehaviourHeader(class_name);
      source = CppBehaviourSource(class_name, snake);
      break;
    case ScriptKind::kCppSystem:
      header = CppSystemHeader(class_name);
      source = CppSystemSource(class_name, snake);
      break;
    case ScriptKind::kCppEditorSystem:
      header = CppEditorSystemHeader(class_name);
      source = CppEditorSystemSource(class_name, snake);
      break;
    case ScriptKind::kAngelScript:
      break;
  }
  if (!WriteText(paths.header, header, error)) return {};
  if (!WriteText(paths.source, source, error)) {
    // Do not leave half a component behind.
    std::error_code remove_ec;
    fs::remove(paths.header, remove_ec);
    return {};
  }
  return paths.source;
}

bool CreateUxml(const fs::path& file) {
  const std::string text = R"(<ui:UXML xmlns:ui="Mercury.UI">
    <ui:VisualElement name="root" style="flex-grow: 1;" />
</ui:UXML>
)";
  return WriteText(file, text, nullptr);
}

bool CreateUss(const fs::path& file) {
  return WriteText(file, "/* Style sheet for UI Toolkit (USS). */\n", nullptr);
}

bool CreateInputActions(const fs::path& file) {
  InputActionsDesc desc;
  desc.maps = {"Player"};
  InputActionDesc move;
  move.map = "Player";
  move.name = "Move";
  move.type = InputAction::Type::kValue;
  move.vector2 = true;
  move.bindings = {"<Gamepad>/leftStick"};
  move.composites.push_back({{"<Keyboard>/w", "<Keyboard>/upArrow"},
                             {"<Keyboard>/s", "<Keyboard>/downArrow"},
                             {"<Keyboard>/a", "<Keyboard>/leftArrow"},
                             {"<Keyboard>/d", "<Keyboard>/rightArrow"}});
  InputActionDesc look;
  look.map = "Player";
  look.name = "Look";
  look.type = InputAction::Type::kValue;
  look.vector2 = true;
  look.bindings = {"<Mouse>/delta", "<Gamepad>/rightStick"};
  InputActionDesc jump;
  jump.map = "Player";
  jump.name = "Jump";
  jump.bindings = {"<Keyboard>/space", "<Gamepad>/buttonSouth"};
  InputActionDesc fire;
  fire.map = "Player";
  fire.name = "Fire";
  fire.bindings = {"<Mouse>/leftButton", "<Gamepad>/rightTrigger"};
  desc.actions = {move, look, jump, fire};
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  return SaveInputActions(file, desc);
}

bool CreateAsset(const fs::path& file, const std::string& type) {
  Asset asset;
  asset.type = type;
  if (auto info = TypeRegistry::Instance().FindAssetType(type)) {
    for (const auto& field : info->fields) {
      asset.properties.Set(field.name, field.default_value);
    }
  }
  return SaveAssetFile(file, asset);
}

}  // namespace Mercury::Templates

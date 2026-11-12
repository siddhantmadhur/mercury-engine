#include "templates.h"

#include <cctype>
#include <format>
#include <fstream>

#include "input/input_actions.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "scripting/type_registry.h"

namespace Mercury::Templates {

namespace fs = std::filesystem;

namespace {

bool WriteText(const fs::path& file, const std::string& text,
               std::string* error) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary);
  out << text;
  if (!out) {
    if (error) *error = "could not write " + file.string();
    return false;
  }
  return true;
}

std::string ToTargetName(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
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

std::string NewMapText(const std::string& root_name, bool is_3d) {
  Map map;
  map.RenameNode(map.GetRoot(), root_name);
  map.GetRoot()->components.push_back({"Transform", {}});

  auto add_node = [&](const char* name, glm::vec3 position,
                      glm::vec3 rotation = glm::vec3(0.0f),
                      glm::vec3 scale = glm::vec3(1.0f)) {
    MapNode* node = map.AddNode(map.GetRoot(), name);
    ComponentData transform{"Transform", {}};
    if (position != glm::vec3(0.0f)) transform.properties.Set("position", position);
    if (rotation != glm::vec3(0.0f)) transform.properties.Set("rotation", rotation);
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

  add_node("Camera", {0, 3, 8}, {-15, 0, 0})->components.push_back({"Camera", {}});
  add_node("Sun", {0, 6, 0}, {-50, -30, 0})
      ->components.push_back({"DirectionalLight", {}});

  MapNode* ground = add_node("Ground", glm::vec3(0.0f), glm::vec3(0.0f),
                             {20, 1, 20});
  ComponentData plane{"MeshRenderer", {}};
  plane.properties.Set("mesh", std::string("plane"));
  plane.properties.Set("color", mtext::Color{glm::vec4(0.55f, 0.58f, 0.62f, 1.0f)});
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
  fs::create_directories(root / "assets", ec);
  fs::create_directories(root / "maps", ec);
  if (ec) {
    if (error) *error = "could not create " + root.string();
    return {};
  }

  const fs::path mproj = root / (ToTargetName(name) + std::string(kProjectExtension));
  auto project = Project::Create(mproj, name);
  if (!project->Save()) {
    if (error) *error = "could not write " + mproj.string();
    return {};
  }

  // Build output and editor caches are generated, so keep them out of git.
  if (!WriteText(root / ".gitignore",
                 "# Build output and editor caches\n.mercury/\n",
                 error)) {
    return {};
  }
  if (!WriteText(root / "maps" / "main.mmap", NewMapText(name, is_3d), error)) {
    return {};
  }
  if (!CreateAsset(root / "assets" / "default.masset", "Material")) {
    if (error) *error = "could not write the default material";
    return {};
  }
  return mproj;
}

fs::path CreateScript(const fs::path& project_root,
                      const std::string& class_name, std::string* error) {
  if (!IsValidIdentifier(class_name)) {
    if (error) *error = "\"" + class_name + "\" is not a valid AngelScript class name";
    return {};
  }
  const fs::path source = project_root / "scripts" / (ToSnakeCase(class_name) + ".as");
  if (fs::exists(source)) {
    if (error) *error = source.filename().string() + " already exists";
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
  if (!WriteText(source, text, error)) return {};
  return source;
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

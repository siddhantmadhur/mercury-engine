/*
 * ts_types.cc
 */
#include "scripting/ts_types.h"

#include <format>
#include <fstream>
#include <sstream>

#include "scripting/type_registry.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

bool WriteIfChanged(const fs::path& path, const std::string& content, std::string* error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  {
    std::ifstream in(path, std::ios::binary);
    if (in) {
      std::stringstream existing;
      existing << in.rdbuf();
      if (existing.str() == content) return true;
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
  if (!out) {
    if (error) *error = std::format("Could not write {}", path.string());
    return false;
  }
  return true;
}

std::string TsType(const FieldInfo& field) {
  switch (field.type) {
    case FieldType::kBool: return "boolean";
    case FieldType::kInt:
    case FieldType::kFloat: return "number";
    case FieldType::kString: return "string";
    case FieldType::kEnum: {
      if (field.enum_values.empty()) return "string";
      std::string text;
      for (const auto& value : field.enum_values) {
        if (!text.empty()) text += " | ";
        text += "\"" + value + "\"";
      }
      return text;
    }
    case FieldType::kVector2: return "Vector2";
    case FieldType::kVector3: return "Vector3";
    case FieldType::kColor: return "Color";
    case FieldType::kNodeRef:
    case FieldType::kAsset: return "string";
    default: return "unknown";
  }
}

std::string FromNative(const FieldInfo& field, const std::string& read) {
  switch (field.type) {
    case FieldType::kVector2: return std::format("((v) => new Vector2(v[0], v[1]))({} as number[])", read);
    case FieldType::kVector3: return std::format("((v) => new Vector3(v[0], v[1], v[2]))({} as number[])", read);
    case FieldType::kColor: return std::format("((v) => new Color(v[0], v[1], v[2], v[3]))({} as number[])", read);
    default: return std::format("{} as {}", read, TsType(field));
  }
}

std::string ToNative(const FieldInfo& field) {
  switch (field.type) {
    case FieldType::kVector2: return "[value.x, value.y]";
    case FieldType::kVector3: return "[value.x, value.y, value.z]";
    case FieldType::kColor: return "[value.r, value.g, value.b, value.a]";
    default: return "value";
  }
}

bool IsIdentifier(const std::string& name) {
  if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

// "apply_root_motion" -> "applyRootMotion": every TypeScript API is camelCase.
std::string CamelCase(const std::string& name) {
  std::string out;
  bool upper = false;
  for (char c : name) {
    if (c == '_') {
      upper = !out.empty();
    } else {
      out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
      upper = false;
    }
  }
  return out;
}

std::string Jsdoc(const std::string& text, const std::string& indent) {
  if (text.empty()) return {};
  return std::format("{}/** {} */\n", indent, text);
}

}  // namespace

// Hand-written members merged into the generated classes (the runtime methods of components).
const char* ExtraMembers(const std::string& component) {
  if (component == "Animator") {
    return R"(
  /** Sets a float parameter of the controller. */
  setFloat(name: string, value: number): void { __native.animatorSet(this.gameObject.sceneSerial, this.gameObject.entity, 0, name, value); }
  setInteger(name: string, value: number): void { __native.animatorSet(this.gameObject.sceneSerial, this.gameObject.entity, 1, name, value); }
  setBool(name: string, value: boolean): void { __native.animatorSet(this.gameObject.sceneSerial, this.gameObject.entity, 2, name, value); }
  setTrigger(name: string): void { __native.animatorSet(this.gameObject.sceneSerial, this.gameObject.entity, 3, name); }
  resetTrigger(name: string): void { __native.animatorSet(this.gameObject.sceneSerial, this.gameObject.entity, 4, name); }
  getFloat(name: string): number { return __native.animatorGet(this.gameObject.sceneSerial, this.gameObject.entity, 0, name) as number; }
  getInteger(name: string): number { return __native.animatorGet(this.gameObject.sceneSerial, this.gameObject.entity, 1, name) as number; }
  getBool(name: string): boolean { return __native.animatorGet(this.gameObject.sceneSerial, this.gameObject.entity, 2, name) as boolean; }
  /** Jumps a layer straight to a state (all layers when `layer` is -1). */
  play(state: string, layer = -1, normalizedTime = 0): void { __native.animatorPlay(this.gameObject.sceneSerial, this.gameObject.entity, state, layer, normalizedTime); }
  /** The name of the layer's current state. */
  getCurrentState(layer = 0): string { return __native.animatorState(this.gameObject.sceneSerial, this.gameObject.entity, layer); }
)";
  }
  if (component == "AudioSource") {
    return R"(
  play(): void { __native.audioCall(this.gameObject.sceneSerial, this.gameObject.entity, 0); }
  stop(): void { __native.audioCall(this.gameObject.sceneSerial, this.gameObject.entity, 1); }
  pause(): void { __native.audioCall(this.gameObject.sceneSerial, this.gameObject.entity, 2); }
  unPause(): void { __native.audioCall(this.gameObject.sceneSerial, this.gameObject.entity, 3); }
  get isPlaying(): boolean { return __native.audioCall(this.gameObject.sceneSerial, this.gameObject.entity, 4) as boolean; }
  /** Plays a clip once, on top of whatever is playing. */
  playOneShot(clip: string, volumeScale = 1): void { __native.audioPlayOneShot(this.gameObject.sceneSerial, this.gameObject.entity, clip, volumeScale); }
  setVolume(volume: number): void { __native.audioSet(this.gameObject.sceneSerial, this.gameObject.entity, "volume", volume); }
  setPitch(pitch: number): void { __native.audioSet(this.gameObject.sceneSerial, this.gameObject.entity, "pitch", pitch); }
  setLoop(loop: boolean): void { __native.audioSet(this.gameObject.sceneSerial, this.gameObject.entity, "loop", loop); }
  setMute(mute: boolean): void { __native.audioSet(this.gameObject.sceneSerial, this.gameObject.entity, "mute", mute); }
  setClip(clip: string): void { __native.audioSet(this.gameObject.sceneSerial, this.gameObject.entity, "clip", clip); }
)";
  }
  if (component == "CharacterController") {
    return R"(
  /** Moves the controller by `motion`, stopping at obstacles; returns the collision flags. */
  move(motion: Vector3): number { return __native.characterMove(this.gameObject.sceneSerial, this.gameObject.entity, motion.x, motion.y, motion.z); }
  get isGrounded(): boolean { return __native.characterGrounded(this.gameObject.sceneSerial, this.gameObject.entity); }
  get velocity(): Vector3 {
    __native.characterVelocity(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = __native.scratch;
    return new Vector3(s[0], s[1], s[2]);
  }
)";
  }
  return "";
}

std::string GenerateComponentsTs() {
  std::string out =
      "// Generated from the engine's TypeRegistry. Do not edit: it is rewritten on every build.\n"
      "import { Component } from \"../components\";\n"
      "import { Color, Vector2, Vector3 } from \"../math\";\n\n";
  for (const ComponentInfo* info : TypeRegistry::Instance().Components()) {
    if (info->is_script || !info->has || !IsIdentifier(info->name)) continue;
    // These have hand-written classes (gameObject.transform, behaviour.document).
    if (info->name == "Transform" || info->name == "UIDocument") continue;
    out += Jsdoc(info->description, "");
    out += std::format("export class {0} extends Component {{\n  static readonly typeName = \"{0}\";\n", info->name);
    for (const FieldInfo& field : info->fields) {
      if (!IsIdentifier(CamelCase(field.name))) continue;
      out += "\n" + Jsdoc(field.tooltip, "  ");
      const std::string read = std::format("this.getField(\"{}\", \"{}\")", info->name, field.name);
      const std::string property = CamelCase(field.name);
      out += std::format("  get {}(): {} {{ return {}; }}\n", property, TsType(field), FromNative(field, read));
      if (field.runtime_writable) {
        out += std::format("  set {}(value: {}) {{ this.setField(\"{}\", \"{}\", {}); }}\n", property,
                           TsType(field), info->name, field.name, ToNative(field));
      }
    }
    out += ExtraMembers(info->name);
    out += "}\n\n";
  }
  return out;
}

bool WriteTypeScriptTypes(const fs::path& out_dir, std::string* error) {
  RegisterBuiltinTypes();  // the component classes are generated from the registry
  const fs::path root = out_dir / "mercury";
  for (size_t i = 0; i < kTsRuntimeFileCount; i++) {
    const EmbeddedFile& file = kTsRuntimeFiles[i];
    if (std::string_view(file.path) == "generated/components.ts") continue;
    std::string content(reinterpret_cast<const char*>(file.data), file.size);
    if (!WriteIfChanged(root / file.path, content, error)) return false;
  }
  return WriteIfChanged(root / "generated" / "components.ts", GenerateComponentsTs(), error);
}

bool WriteDefaultTsconfig(const fs::path& project_dir, std::string* error) {
  const fs::path path = project_dir / "tsconfig.json";
  if (fs::exists(path)) return true;
  static const char* kTsconfig = R"({
  "compilerOptions": {
    "target": "ES2023",
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "lib": ["ES2023", "ESNext.Decorators"],
    "types": [],
    "strict": true,
    "noEmit": true,
    "useDefineForClassFields": true,
    "isolatedModules": true,
    "skipLibCheck": true,
    "paths": { "mercury": ["./.mercury/types/mercury/index.ts"] }
  },
  "include": ["scripts/**/*.ts", ".mercury/types/**/*.ts"]
}
)";
  return WriteIfChanged(path, kTsconfig, error);
}

}  // namespace Mercury

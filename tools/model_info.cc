// mercury_model_info: prints what Mercury's model loader sees in a model file
// as JSON (nodes, meshes with their materials, clips), for import tools.
//
//   mercury_model_info [--unity] model.fbx [more.fbx ...]
//
// --unity loads with the Unity convention (see properties/model.h).

#include <cstdio>
#include <string>
#include <string_view>

#include "properties/model.h"

namespace {

std::string Escape(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (static_cast<unsigned char>(c) < 0x20) continue;
    out.push_back(c);
  }
  return out;
}

void PrintModel(const std::string& path, bool unity) {
  Mercury::ModelImportSettings settings;
  settings.convention = unity ? Mercury::ModelConvention::kUnity : Mercury::ModelConvention::kNative;
  std::string error;
  auto model = Mercury::Model::Load(path, settings, &error);
  if (!model) {
    std::printf("{\"path\": \"%s\", \"error\": \"%s\"}", Escape(path).c_str(),
                Escape(error).c_str());
    return;
  }
  std::printf("{\"path\": \"%s\", \"nodes\": [", Escape(path).c_str());
  const auto& nodes = model->Nodes();
  for (size_t i = 0; i < nodes.size(); i++) {
    const auto& n = nodes[i];
    std::printf("%s{\"name\": \"%s\", \"parent\": %d, \"position\": [%g, %g, %g], "
                "\"rotation\": [%g, %g, %g, %g], \"scale\": [%g, %g, %g], \"meshes\": [",
                i ? ", " : "", Escape(n.name).c_str(), n.parent, n.position.x, n.position.y,
                n.position.z, n.rotation.x, n.rotation.y, n.rotation.z, n.rotation.w, n.scale.x,
                n.scale.y, n.scale.z);
    for (size_t m = 0; m < n.meshes.size(); m++) std::printf("%s%d", m ? ", " : "", n.meshes[m]);
    std::printf("]}");
  }
  std::printf("], \"meshes\": [");
  const auto& meshes = model->Meshes();
  for (size_t i = 0; i < meshes.size(); i++) {
    const auto& m = meshes[i];
    const auto& g = *m.geometry;
    std::printf("%s{\"name\": \"%s\", \"node\": %d, \"material\": %d, \"skinned\": %s, "
                "\"vertices\": %zu, \"bounds\": [[%g, %g, %g], [%g, %g, %g]]}",
                i ? ", " : "", Escape(m.name).c_str(), m.node, m.material,
                m.bones.empty() ? "false" : "true", g.vertices.size(), g.bounds_min.x,
                g.bounds_min.y, g.bounds_min.z, g.bounds_max.x, g.bounds_max.y, g.bounds_max.z);
  }
  std::printf("], \"materials\": [");
  const auto& materials = model->Materials();
  for (size_t i = 0; i < materials.size(); i++) {
    const auto& m = materials[i];
    std::printf("%s{\"name\": \"%s\", \"color\": [%g, %g, %g, %g], \"texture\": \"%s\"}",
                i ? ", " : "", Escape(m.name).c_str(), m.color.r, m.color.g, m.color.b,
                m.color.a, m.texture ? Escape(m.texture->source).c_str() : "");
  }
  std::printf("], \"clips\": [");
  const auto& clips = model->Clips();
  for (size_t i = 0; i < clips.size(); i++) {
    std::printf("%s{\"name\": \"%s\", \"duration\": %g}", i ? ", " : "",
                Escape(clips[i].name).c_str(), clips[i].duration);
  }
  std::printf("]}");
}

}  // namespace

int main(int argc, char** argv) {
  bool unity = false;
  bool first = true;
  std::printf("[");
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "--unity") {
      unity = true;
      continue;
    }
    if (!first) std::printf(",\n");
    first = false;
    PrintModel(argv[i], unity);
  }
  std::printf("]\n");
  return 0;
}

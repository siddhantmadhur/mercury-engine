#include "model.h"

#include <assimp/ObjMaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <set>
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "algorithms/parallel_for.h"
#include "properties/parallel_obj.h"
#include "properties/texture2d.h"
#include "properties/vfs_io_system.h"
#include "serialization/mtext.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

namespace Mercury {

namespace fs = std::filesystem;

// ------------------------------------------------------------- ImageData ---

uint64_t ImageData::NextId() {
  static std::atomic<uint64_t> next{1};
  return next++;
}

std::shared_ptr<ImageData> ImageData::FromPixels(int width, int height,
                                                 std::vector<uint8_t> pixels,
                                                 std::string source) {
  auto image = std::make_shared<ImageData>();
  image->width = width;
  image->height = height;
  image->pixels = std::move(pixels);
  image->id = NextId();
  image->source = std::move(source);
  return image;
}

std::shared_ptr<ImageData> ImageData::Load(const std::string& path) {
  int width = 0, height = 0, channels = 0;
  stbi_uc* data = LoadImageFile(path, &width, &height, &channels, 4);
  if (!data) return nullptr;
  std::vector<uint8_t> pixels(data, data + size_t(width) * height * 4);
  stbi_image_free(data);
  return FromPixels(width, height, std::move(pixels), path);
}

std::shared_ptr<ImageData> ImageData::FromMemory(const uint8_t* bytes, size_t size,
                                                 std::string source) {
  int width = 0, height = 0, channels = 0;
  stbi_uc* data = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height,
                                        &channels, 4);
  if (!data) return nullptr;
  std::vector<uint8_t> pixels(data, data + size_t(width) * height * 4);
  stbi_image_free(data);
  return FromPixels(width, height, std::move(pixels), std::move(source));
}

// ---------------------------------------------------------- MeshGeometry ---

void MeshGeometry::ComputeBounds() {
  if (vertices.empty()) {
    bounds_min = bounds_max = glm::vec3(0.0f);
    return;
  }
  bounds_min = bounds_max = vertices[0].position;
  for (const auto& v : vertices) {
    bounds_min = glm::min(bounds_min, v.position);
    bounds_max = glm::max(bounds_max, v.position);
  }
}

void MeshGeometry::ComputeTangents() {
  // Per vertex: the surface directions of increasing u and of "up" in the
  // image (decreasing v: row 0 is the top), summed over its triangles.
  std::vector<glm::vec3> along_u(vertices.size(), glm::vec3(0.0f));
  std::vector<glm::vec3> up(vertices.size(), glm::vec3(0.0f));
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
    if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
    const glm::vec3 e1 = vertices[b].position - vertices[a].position;
    const glm::vec3 e2 = vertices[c].position - vertices[a].position;
    const glm::vec2 d1 = vertices[b].uv - vertices[a].uv;
    const glm::vec2 d2 = vertices[c].uv - vertices[a].uv;
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (std::abs(det) < 1e-12f) continue;
    const float r = 1.0f / det;
    const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
    const glm::vec3 bv = (e2 * d1.x - e1 * d2.x) * r;
    for (uint32_t v : {a, b, c}) {
      along_u[v] += t;
      up[v] -= bv;
    }
  }
  for (size_t v = 0; v < vertices.size(); v++) {
    const glm::vec3 n = vertices[v].normal;
    glm::vec3 t = along_u[v] - n * glm::dot(n, along_u[v]);
    if (glm::dot(t, t) < 1e-20f) {
      // No usable UVs: any direction across the surface.
      t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0))
                               : glm::cross(n, glm::vec3(0, 1, 0));
    }
    t = glm::normalize(t);
    const float w = glm::dot(glm::cross(n, t), up[v]) < 0.0f ? -1.0f : 1.0f;
    vertices[v].tangent = glm::vec4(t, w);
  }
}

std::shared_ptr<MeshGeometry> MeshGeometry::Create(std::vector<ModelVertex> vertices,
                                                   std::vector<uint32_t> indices) {
  auto geometry = std::make_shared<MeshGeometry>();
  geometry->vertices = std::move(vertices);
  geometry->indices = std::move(indices);
  geometry->id = ImageData::NextId();
  geometry->ComputeBounds();
  return geometry;
}

// ------------------------------------------------------------- ModelNode ---

glm::mat4 ModelNode::LocalMatrix() const {
  return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1.0f), scale);
}

// --------------------------------------------------------- AnimationClip ---

namespace {

template <typename T, typename Lerp>
T SampleKeys(const std::vector<float>& times, const std::vector<T>& values, float t,
             Lerp lerp) {
  if (values.size() == 1 || t <= times.front()) return values.front();
  if (t >= times.back()) return values.back();
  auto it = std::upper_bound(times.begin(), times.end(), t);
  const size_t next = static_cast<size_t>(it - times.begin());
  const size_t prev = next - 1;
  const float span = times[next] - times[prev];
  const float f = span > 0.0f ? (t - times[prev]) / span : 0.0f;
  return lerp(values[prev], values[next], f);
}

}  // namespace

void AnimationClip::Sample(const AnimationChannel& channel, float t, glm::vec3& position,
                           glm::quat& rotation, glm::vec3& scale) const {
  auto lerp3 = [](const glm::vec3& a, const glm::vec3& b, float f) {
    return glm::mix(a, b, f);
  };
  if (!channel.positions.empty()) {
    position = SampleKeys(channel.position_times, channel.positions, t, lerp3);
  }
  if (!channel.rotations.empty()) {
    rotation = SampleKeys(channel.rotation_times, channel.rotations, t,
                          [](const glm::quat& a, const glm::quat& b, float f) {
                            return glm::normalize(glm::slerp(a, b, f));
                          });
  }
  if (!channel.scales.empty()) {
    scale = SampleKeys(channel.scale_times, channel.scales, t, lerp3);
  }
}

// --------------------------------------------------- ModelImportSettings ---

ModelImportSettings ModelImportSettings::ForFile(const std::string& model_path,
                                                 const ModelImportSettings& defaults) {
  ModelImportSettings settings = defaults;
  const std::string sidecar = model_path + ".mimport";
  auto text = os::ReadText(sidecar);
  if (!text) return settings;
  mtext::Document document;
  if (!mtext::Parse(*text, document)) {
    PDebug::warn("Could not parse {}", sidecar);
    return settings;
  }
  if (auto section = document.Find("mimport")) {
    const std::string convention =
        section->properties.GetString("convention", settings.convention == ModelConvention::kUnity
                                                        ? "unity" : "native");
    settings.convention =
        convention == "unity" ? ModelConvention::kUnity : ModelConvention::kNative;
    settings.scale = static_cast<float>(section->properties.GetFloat("scale", settings.scale));
    settings.use_file_scale =
        section->properties.GetBool("use_file_scale", settings.use_file_scale);
    settings.max_texture_size = static_cast<int>(
        section->properties.GetInt("max_texture_size", settings.max_texture_size));
  }
  for (const mtext::Section* section : document.FindAll("material")) {
    ModelMaterialOverride entry;
    entry.name = section->attributes.GetString("name");
    if (entry.name.empty()) {
      PDebug::warn("{}:{}: [material] needs a name", sidecar, section->line);
      continue;
    }
    const mtext::PropertyList& p = section->properties;
    if (auto v = p.Get("color"); v && v->Get<mtext::Color>()) {
      entry.color = v->Get<mtext::Color>()->rgba;
    }
    if (auto v = p.Get("emission"); v && v->Get<mtext::Color>()) {
      entry.emission = glm::vec3(v->Get<mtext::Color>()->rgba);
    }
    if (p.Has("metallic")) entry.metallic = static_cast<float>(p.GetFloat("metallic"));
    if (p.Has("smoothness")) entry.smoothness = static_cast<float>(p.GetFloat("smoothness"));
    settings.materials.push_back(std::move(entry));
  }
  return settings;
}

// ----------------------------------------------------------------- Model ---

namespace {

// Linear to sRGB, for colors stored the way ModelMaterial::color expects.
float LinearToSrgb(float c) {
  c = std::clamp(c, 0.0f, 1.0f);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

glm::mat4 ToGlm(const aiMatrix4x4& m) {
  // assimp is row-major, glm column-major.
  return glm::transpose(glm::make_mat4(&m.a1));
}

void Decompose(const glm::mat4& m, glm::vec3& position, glm::quat& rotation,
               glm::vec3& scale) {
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::decompose(m, scale, rotation, position, skew, perspective);
  rotation = glm::normalize(rotation);
}

// Rotation by 180 degrees about Y: Unity's left-handed space (X flipped from
// the file) seen in Mercury's right-handed one (Z flipped from Unity).
glm::vec3 FlipPosition(glm::vec3 p) { return {-p.x, p.y, -p.z}; }
glm::quat FlipRotation(glm::quat q) { return glm::quat(q.w, -q.x, q.y, -q.z); }
glm::mat4 FlipMatrix(const glm::mat4& m) {
  const glm::mat4 r = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, -1.0f));
  return r * m * r;
}

std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// `dir / file`, matching the name without regard to case when the exact one
// is missing (models made on Windows often spell their textures differently).
// Through os:: so that a shipped game finds them in its .mpack.
fs::path FindInDirectory(const fs::path& dir, const fs::path& file) {
  const fs::path exact = dir / file;
  if (os::Exists(exact)) return exact;
  const std::string wanted = Lower(file.string());
  for (const std::string& name : os::ListDirectory(dir)) {
    if (Lower(name) == wanted) return dir / name;
  }
  return {};
}

// Finds a texture a model refers to, the way Unity's importer searches:
// next to the model, in a "Textures" folder, then in the folders above.
std::string FindTexture(const fs::path& model_dir, const std::string& reference) {
  std::string name = reference;
  std::replace(name.begin(), name.end(), '\\', '/');
  const fs::path file = fs::path(name).filename();
  if (file.empty()) return "";
  // A relative path as written in the file.
  if (!fs::path(name).is_absolute()) {
    const fs::path direct = model_dir / name;
    if (auto found = FindInDirectory(direct.parent_path(), file); !found.empty()) {
      return found.string();
    }
  }
  fs::path dir = model_dir;
  for (int depth = 0; depth < 6 && !dir.empty(); depth++) {
    for (const char* sub : {"", "Textures", "textures", "Texture", "texture"}) {
      if (auto found = FindInDirectory(dir / sub, file); !found.empty()) {
        return found.string();
      }
    }
    if (dir == dir.parent_path()) break;
    dir = dir.parent_path();
  }
  return "";
}

// Scales `image` down (keeping its aspect) so neither side exceeds `max_size`.
std::shared_ptr<ImageData> FitTexture(std::shared_ptr<ImageData> image, int max_size) {
  if (!image || max_size <= 0 || std::max(image->width, image->height) <= max_size) {
    return image;
  }
  const float factor = static_cast<float>(max_size) / std::max(image->width, image->height);
  const int width = std::max(1, static_cast<int>(image->width * factor));
  const int height = std::max(1, static_cast<int>(image->height * factor));
  std::vector<uint8_t> pixels(size_t(width) * height * 4);
  if (image->srgb) {
    stbir_resize_uint8_srgb(image->pixels.data(), image->width, image->height, 0,
                            pixels.data(), width, height, 0, STBIR_RGBA);
  } else {
    stbir_resize_uint8_linear(image->pixels.data(), image->width, image->height, 0,
                              pixels.data(), width, height, 0, STBIR_RGBA);
  }
  PDebug::log("{}: scaled {}x{} down to {}x{}", image->source, image->width, image->height,
              width, height);
  auto result = ImageData::FromPixels(width, height, std::move(pixels), image->source);
  result->srgb = image->srgb;
  return result;
}

// True when the image is already a tangent-space normal map (bluish, with
// x and y centered), rather than a height map.
bool LooksLikeNormalMap(const ImageData& image) {
  const size_t count = size_t(image.width) * image.height;
  if (count == 0) return false;
  const size_t step = std::max<size_t>(1, count / 4096);
  double r = 0.0, g = 0.0, b = 0.0;
  size_t samples = 0;
  for (size_t i = 0; i < count; i += step, samples++) {
    r += image.pixels[i * 4 + 0];
    g += image.pixels[i * 4 + 1];
    b += image.pixels[i * 4 + 2];
  }
  r /= samples * 255.0;
  g /= samples * 255.0;
  b /= samples * 255.0;
  return b > 0.7 && std::abs(r - 0.5) < 0.15 && std::abs(g - 0.5) < 0.15;
}

// Turns a height (bump) map, light = raised, into a tangent-space normal map
// with x along +u and y toward the top of the image.
std::shared_ptr<ImageData> HeightToNormalMap(const ImageData& height, float strength) {
  const int w = height.width, h = height.height;
  std::vector<float> lum(size_t(w) * h);
  for (size_t i = 0; i < lum.size(); i++) {
    lum[i] = (0.299f * height.pixels[i * 4 + 0] + 0.587f * height.pixels[i * 4 + 1] +
              0.114f * height.pixels[i * 4 + 2]) / 255.0f;
  }
  // Textures repeat, so the filter wraps around the edges.
  auto at = [&](int x, int y) { return lum[size_t((y + h) % h) * w + (x + w) % w]; };
  std::vector<uint8_t> pixels(size_t(w) * h * 4);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // Sobel; rows grow downward, so "up" is y - 1.
      const float dx = (at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1)) -
                       (at(x - 1, y - 1) + 2.0f * at(x - 1, y) + at(x - 1, y + 1));
      const float dy = (at(x - 1, y - 1) + 2.0f * at(x, y - 1) + at(x + 1, y - 1)) -
                       (at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1));
      const glm::vec3 n = glm::normalize(glm::vec3(-dx * strength, -dy * strength, 1.0f));
      uint8_t* out = &pixels[(size_t(y) * w + x) * 4];
      out[0] = static_cast<uint8_t>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
      out[1] = static_cast<uint8_t>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
      out[2] = static_cast<uint8_t>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
      out[3] = 255;
    }
  }
  auto image = ImageData::FromPixels(w, h, std::move(pixels), height.source + " (normals)");
  image->srgb = false;
  return image;
}

}  // namespace

std::shared_ptr<Model> Model::Load(const std::string& path,
                                   const ModelImportSettings& settings,
                                   std::string* error, std::atomic<float>* progress) {
  const auto start_time = std::chrono::steady_clock::now();
  auto seconds_since = [](std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
  };
  // The load is reported as three stages of the progress bar: the file,
  // its textures, its meshes.
  auto report = [progress](float done) {
    if (progress) progress->store(done);
  };
  constexpr unsigned kImportFlags =
      aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
      aiProcess_FlipUVs | aiProcess_LimitBoneWeights | aiProcess_SortByPType |
      aiProcess_ValidateDataStructure;
  auto configure = [](Assimp::Importer& importer) {
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    // Files without normals keep hard edges above 60 degrees (Unity's default
    // smoothing angle), so boxes don't shade like pillows.
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 60.0f);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                                aiPrimitiveType_POINT | aiPrimitiveType_LINE);
  };

  // A big .obj is read in pieces on every core; everything else (and small
  // or unsplittable .obj files) by one importer.
  const std::string extension = Lower(fs::path(path).extension().string());
  report(0.0f);
  std::unique_ptr<aiScene> pieced_scene;
  if (extension == ".obj") {
    ParallelObjOptions options;
    options.min_bytes = settings.parallel_obj_min_bytes;
    options.max_pieces = settings.parallel_obj_pieces;
    options.flags = kImportFlags;
    options.configure = configure;
    options.progress = [&report](float done) { report(done * 0.5f); };
    pieced_scene = ImportObjInParallel(path, options);
  }
  Assimp::Importer importer;
  const aiScene* scene = pieced_scene.get();
  if (!scene) {
    UseVfs(importer);
    configure(importer);
    scene = importer.ReadFile(path, kImportFlags);
  }
  const double import_seconds = seconds_since(start_time);
  report(0.5f);
  if (!scene || !scene->mRootNode) {
    if (error) *error = importer.GetErrorString();
    return nullptr;
  }

  auto model = std::make_shared<Model>();
  model->path_ = path;
  const bool unity = settings.convention == ModelConvention::kUnity;
  model->root_on_object_ = unity;
  const bool is_fbx = fs::path(path).extension() == ".fbx" ||
                      fs::path(path).extension() == ".FBX";

  // Unity converts the file's units to meters (FBX stores centimeters per
  // unit in UnitScaleFactor).
  float file_scale = 1.0f;
  if (settings.use_file_scale && is_fbx && scene->mMetaData) {
    float unit = 1.0f;
    if (scene->mMetaData->Get("UnitScaleFactor", unit) && unit > 0.0f) {
      file_scale = unit / 100.0f;
    }
  }
  const float scale = file_scale * settings.scale;
  // Unity turns the file's up axis into +Y with a single rotation and,
  // unlike assimp, ignores which way the file says is forward.
  glm::mat4 axis = ToGlm(scene->mRootNode->mTransformation);
  if (unity && is_fbx && scene->mMetaData) {
    int up = 1, up_sign = 1;
    scene->mMetaData->Get("UpAxis", up);
    scene->mMetaData->Get("UpAxisSign", up_sign);
    const float sign = up_sign < 0 ? -1.0f : 1.0f;
    axis = glm::mat4(1.0f);
    if (up == 2) {
      axis = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f * sign), glm::vec3(1, 0, 0));
    } else if (up == 0) {
      axis = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f * sign), glm::vec3(0, 0, 1));
    } else if (sign < 0.0f) {
      axis = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0));
    }
  }

  // ---- Nodes
  std::map<const aiNode*, int> node_index;
  std::vector<std::pair<const aiNode*, int>> node_order;  // creation order
  std::function<void(const aiNode*, int, bool)> add_node = [&](const aiNode* source,
                                                               int parent, bool top) {
    ModelNode node;
    node.name = source->mName.C_Str();
    node.parent = parent;
    glm::mat4 local = ToGlm(source->mTransformation);
    if (unity && top) local = axis * local;  // Unity's axis conversion
    Decompose(local, node.position, node.rotation, node.scale);
    node.position *= scale;
    if (unity) {
      node.position = FlipPosition(node.position);
      node.rotation = FlipRotation(node.rotation);
    }
    const int index = static_cast<int>(model->nodes_.size());
    node_index[source] = index;
    node_order.emplace_back(source, index);
    model->nodes_.push_back(node);
    if (parent >= 0) model->nodes_[parent].children.push_back(index);
    for (unsigned i = 0; i < source->mNumChildren; i++) {
      add_node(source->mChildren[i], index, false);
    }
  };

  const aiNode* root = scene->mRootNode;
  if (unity) {
    // A single top node becomes the model's root object, as in Unity;
    // otherwise an empty root named after the file holds the top nodes.
    if (root->mNumChildren == 1 && root->mNumMeshes == 0) {
      add_node(root->mChildren[0], -1, true);
    } else {
      ModelNode synthetic;
      synthetic.name = fs::path(path).stem().string();
      model->nodes_.push_back(synthetic);
      node_index[root] = 0;
      for (unsigned i = 0; i < root->mNumChildren; i++) {
        add_node(root->mChildren[i], 0, true);
      }
    }
  } else {
    add_node(root, -1, false);
  }

  // ---- Materials
  const fs::path model_dir = fs::path(path).parent_path();
  const bool is_obj = extension == ".obj";
  // MTL's Kd and glTF's base color factor are linear reflectance, as the
  // renderers these files come from use them; ModelMaterial::color is sRGB.
  const bool linear_colors = is_obj || extension == ".gltf" || extension == ".glb";
  enum class TextureUse { kColor, kData, kHeight };
  struct TextureRequest {
    std::string reference;
    TextureUse use;
    float bump_scale;
  };
  using TextureKey = std::pair<std::string, TextureUse>;
  std::map<TextureKey, std::shared_ptr<ImageData>> loaded_textures;
  std::vector<TextureRequest> requests;  // what the materials ask for
  bool collecting = true;
  // Decodes a texture the file refers to, embedded or next to it. Height
  // maps come back as normal maps. Runs on several threads at once.
  auto decode_texture = [&](const TextureRequest& request) -> std::shared_ptr<ImageData> {
    std::shared_ptr<ImageData> image;
    if (const aiTexture* embedded = scene->GetEmbeddedTexture(request.reference.c_str())) {
      if (embedded->mHeight == 0) {
        image = ImageData::FromMemory(reinterpret_cast<const uint8_t*>(embedded->pcData),
                                      embedded->mWidth, path + ":" + request.reference);
      } else {
        std::vector<uint8_t> pixels(size_t(embedded->mWidth) * embedded->mHeight * 4);
        for (size_t i = 0; i < size_t(embedded->mWidth) * embedded->mHeight; i++) {
          pixels[i * 4 + 0] = embedded->pcData[i].r;
          pixels[i * 4 + 1] = embedded->pcData[i].g;
          pixels[i * 4 + 2] = embedded->pcData[i].b;
          pixels[i * 4 + 3] = embedded->pcData[i].a;
        }
        image = ImageData::FromPixels(embedded->mWidth, embedded->mHeight,
                                      std::move(pixels), path + ":" + request.reference);
      }
    } else if (auto file = FindTexture(model_dir, request.reference); !file.empty()) {
      image = ImageData::Load(file);
    }
    if (!image) {
      PDebug::warn("{}: texture {} not found", path, request.reference);
    } else {
      image->srgb = request.use == TextureUse::kColor;
      if (request.use == TextureUse::kHeight && !LooksLikeNormalMap(*image)) {
        image = HeightToNormalMap(*FitTexture(image, settings.max_texture_size),
                                  2.0f * request.bump_scale);
      }
      image = FitTexture(image, settings.max_texture_size);
    }
    return image;
  };
  // Each texture is decoded once per use. The materials are built twice: the
  // first time only notes which textures they want, so they can all be
  // decoded at the same time before the second time attaches them.
  auto load_texture = [&](const std::string& reference, TextureUse use,
                          float bump_scale) -> std::shared_ptr<ImageData> {
    if (reference.empty()) return nullptr;
    const TextureKey key(reference, use);
    if (collecting) {
      if (loaded_textures.emplace(key, nullptr).second) {
        requests.push_back({reference, use, bump_scale});
      }
      return nullptr;
    }
    auto it = loaded_textures.find(key);
    return it != loaded_textures.end() ? it->second : nullptr;
  };
  auto texture_of = [](const aiMaterial* material, aiTextureType type) {
    aiString texture_path;
    if (material->GetTexture(type, 0, &texture_path) != AI_SUCCESS) return std::string();
    return std::string(texture_path.C_Str());
  };
  auto build_materials = [&] {
    for (unsigned m = 0; m < scene->mNumMaterials; m++) {
      const aiMaterial* source = scene->mMaterials[m];
      ModelMaterial material;
      aiString name;
      if (source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) material.name = name.C_Str();
      aiColor4D color(1, 1, 1, 1);
      if (source->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS) {
        source->Get(AI_MATKEY_COLOR_DIFFUSE, color);
      }
      float opacity = 1.0f;
      source->Get(AI_MATKEY_OPACITY, opacity);
      material.color = glm::vec4(color.r, color.g, color.b, color.a * opacity);
      material.transparent = material.color.a < 0.999f;
      float metallic = 0.0f;
      if (source->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        material.metallic = metallic;
      }
      float roughness = 0.0f;
      if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        material.smoothness = 1.0f - std::clamp(roughness, 0.0f, 1.0f);
      } else if (is_obj) {
        // Blinn-Phong exponent (Ns) to GGX roughness; without specular (Ks)
        // the surface is matte whatever Ns says.
        float shininess = 0.0f;
        source->Get(AI_MATKEY_SHININESS, shininess);
        aiColor3D specular(0, 0, 0);
        source->Get(AI_MATKEY_COLOR_SPECULAR, specular);
        const float ks = std::max({specular.r, specular.g, specular.b});
        const float smooth = 1.0f - std::sqrt(2.0f / (std::max(shininess, 0.0f) + 2.0f));
        material.smoothness = std::clamp(smooth * std::min(1.0f, ks * 4.0f), 0.0f, 0.95f);
        // No diffuse but a strong specular: a mirror, or glass when the
        // illumination model refracts (4, 6, 7). Approximated as metal and as
        // see-through, there being no reflections or refraction to draw.
        const float kd = std::max({color.r, color.g, color.b});
        if (kd < 0.05f && ks > 0.25f && texture_of(source, aiTextureType_DIFFUSE).empty()) {
          int illum = 2;
          source->Get(AI_MATKEY_OBJ_ILLUM, illum);
          if (illum == 4 || illum == 6 || illum == 7) {
            material.color = glm::vec4(glm::vec3(std::max(ks, 0.6f)), 0.25f);
            material.transparent = true;
          } else {
            material.color = glm::vec4(specular.r, specular.g, specular.b, material.color.a);
            material.metallic = 1.0f;
          }
        }
      }
      if (linear_colors) {
        material.color = glm::vec4(LinearToSrgb(material.color.r), LinearToSrgb(material.color.g),
                                   LinearToSrgb(material.color.b), material.color.a);
      }
      aiColor3D emissive(0, 0, 0);
      source->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
      material.emission = glm::vec3(emissive.r, emissive.g, emissive.b);

      std::string reference = texture_of(source, aiTextureType_BASE_COLOR);
      if (reference.empty()) reference = texture_of(source, aiTextureType_DIFFUSE);
      material.texture = load_texture(reference, TextureUse::kColor, 1.0f);
      // OBJ exporters often write Kd 0 next to map_Kd, meaning "the texture
      // alone" rather than black.
      if (is_obj && material.texture && glm::vec3(material.color) == glm::vec3(0.0f)) {
        material.color = glm::vec4(1.0f, 1.0f, 1.0f, material.color.a);
      }

      material.emission_texture =
          load_texture(texture_of(source, aiTextureType_EMISSIVE), TextureUse::kColor, 1.0f);
      if (material.emission_texture && material.emission == glm::vec3(0.0f)) {
        material.emission = glm::vec3(1.0f);
      }

      if (auto normals = texture_of(source, aiTextureType_NORMALS); !normals.empty()) {
        material.normal_texture = load_texture(normals, TextureUse::kData, 1.0f);
        source->Get(AI_MATKEY_OBJ_BUMPMULT_NORMALS(0), material.normal_scale);
      } else {
        std::string height = texture_of(source, aiTextureType_HEIGHT);
        if (height.empty()) height = texture_of(source, aiTextureType_DISPLACEMENT);
        float bump = 1.0f;
        source->Get(AI_MATKEY_OBJ_BUMPMULT_HEIGHT(0), bump);
        material.normal_texture = load_texture(height, TextureUse::kHeight, bump);
      }
      for (const ModelMaterialOverride& entry : settings.materials) {
        if (entry.name != material.name) continue;
        if (entry.color) {
          material.color = *entry.color;
          material.transparent = material.color.a < 0.999f;
        }
        if (entry.metallic) material.metallic = std::clamp(*entry.metallic, 0.0f, 1.0f);
        if (entry.smoothness) material.smoothness = std::clamp(*entry.smoothness, 0.0f, 1.0f);
        if (entry.emission) material.emission = *entry.emission;
      }
      model->materials_.push_back(std::move(material));
    }
  };
  build_materials();
  collecting = false;
  const double materials_seconds = seconds_since(start_time);
  std::vector<std::shared_ptr<ImageData>> decoded(requests.size());
  std::atomic<size_t> decoded_count{0};
  ParallelFor(requests.size(), [&](size_t i) {
    decoded[i] = decode_texture(requests[i]);
    report(0.5f + 0.3f * static_cast<float>(++decoded_count) /
                      static_cast<float>(requests.size()));
  });
  for (size_t i = 0; i < requests.size(); i++) {
    loaded_textures[TextureKey(requests[i].reference, requests[i].use)] = std::move(decoded[i]);
  }
  model->materials_.clear();
  build_materials();
  const double textures_seconds = seconds_since(start_time) - materials_seconds;
  report(0.8f);
  if (model->materials_.empty()) model->materials_.push_back(ModelMaterial{});

  // ---- Meshes
  // Converted at the same time, a mesh per thread; kept in file order.
  struct MeshJob {
    const aiMesh* source;
    int node;
  };
  std::vector<MeshJob> mesh_jobs;
  for (const auto& [source_node, index] : node_order) {
    for (unsigned i = 0; i < source_node->mNumMeshes; i++) {
      const aiMesh* source = scene->mMeshes[source_node->mMeshes[i]];
      if (source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) mesh_jobs.push_back({source, index});
    }
  }
  std::vector<ModelMesh> built_meshes(mesh_jobs.size());
  std::atomic<size_t> converted{0};
  ParallelFor(mesh_jobs.size(), [&](size_t job_index) {
    const aiMesh* source = mesh_jobs[job_index].source;
    const int index = mesh_jobs[job_index].node;
    std::vector<ModelVertex> vertices(source->mNumVertices);
    for (unsigned v = 0; v < source->mNumVertices; v++) {
      ModelVertex& vertex = vertices[v];
      const aiVector3D& p = source->mVertices[v];
      vertex.position = glm::vec3(p.x, p.y, p.z) * scale;
      if (source->HasNormals()) {
        vertex.normal = glm::vec3(source->mNormals[v].x, source->mNormals[v].y,
                                  source->mNormals[v].z);
      }
      if (source->HasTextureCoords(0)) {
        vertex.uv = glm::vec2(source->mTextureCoords[0][v].x,
                              source->mTextureCoords[0][v].y);
      }
      if (unity) {
        vertex.position = FlipPosition(vertex.position);
        vertex.normal = FlipPosition(vertex.normal);
      }
    }
    std::vector<uint32_t> indices;
    indices.reserve(source->mNumFaces * 3);
    for (unsigned f = 0; f < source->mNumFaces; f++) {
      const aiFace& face = source->mFaces[f];
      if (face.mNumIndices != 3) continue;
      indices.push_back(face.mIndices[0]);
      indices.push_back(face.mIndices[1]);
      indices.push_back(face.mIndices[2]);
    }

    ModelMesh mesh;
    mesh.name = source->mName.C_Str();
    mesh.node = index;
    mesh.material = static_cast<int>(
        std::min<unsigned>(source->mMaterialIndex,
                           static_cast<unsigned>(model->materials_.size() - 1)));
    if (source->HasBones()) {
      std::vector<int> counts(vertices.size(), 0);
      for (unsigned b = 0; b < source->mNumBones; b++) {
        const aiBone* bone = source->mBones[b];
        ModelBone model_bone;
        if (const aiNode* bone_node = root->FindNode(bone->mName)) {
          auto it = node_index.find(bone_node);
          if (it != node_index.end()) model_bone.node = it->second;
        }
        glm::mat4 offset = ToGlm(bone->mOffsetMatrix);
        offset[3] = glm::vec4(glm::vec3(offset[3]) * scale, 1.0f);
        if (unity) offset = FlipMatrix(offset);
        model_bone.offset = offset;
        mesh.bones.push_back(model_bone);
        for (unsigned w = 0; w < bone->mNumWeights; w++) {
          const aiVertexWeight& weight = bone->mWeights[w];
          if (weight.mVertexId >= vertices.size()) continue;
          int& slot = counts[weight.mVertexId];
          if (slot >= 4) continue;
          vertices[weight.mVertexId].joints[slot] = b;
          vertices[weight.mVertexId].weights[slot] = weight.mWeight;
          slot++;
        }
      }
      for (auto& vertex : vertices) {
        const float total = vertex.weights.x + vertex.weights.y + vertex.weights.z +
                            vertex.weights.w;
        if (total > 0.0f) vertex.weights /= total;
      }
    }
    mesh.geometry = MeshGeometry::Create(std::move(vertices), std::move(indices));
    mesh.geometry->skinned = !mesh.bones.empty();
    if (model->materials_[mesh.material].normal_texture) mesh.geometry->ComputeTangents();
    built_meshes[job_index] = std::move(mesh);
    report(0.8f + 0.2f * static_cast<float>(++converted) /
                      static_cast<float>(mesh_jobs.size()));
  });
  for (size_t i = 0; i < built_meshes.size(); i++) {
    const int mesh_index = static_cast<int>(model->meshes_.size());
    model->nodes_[mesh_jobs[i].node].meshes.push_back(mesh_index);
    model->meshes_.push_back(std::move(built_meshes[i]));
  }

  report(1.0f);
  PDebug::log("Loaded {} in {:.1f}s (file {:.1f}s, textures {:.1f}s)", path,
              seconds_since(start_time), import_seconds, textures_seconds);
  // ---- Animations
  std::map<std::string, int> node_by_name;
  for (size_t i = 0; i < model->nodes_.size(); i++) {
    node_by_name.emplace(model->nodes_[i].name, static_cast<int>(i));
  }
  std::set<int> top_nodes;
  if (unity) {
    for (unsigned i = 0; i < root->mNumChildren; i++) {
      if (auto it = node_index.find(root->mChildren[i]); it != node_index.end()) {
        top_nodes.insert(it->second);
      }
    }
  }
  for (unsigned a = 0; a < scene->mNumAnimations; a++) {
    const aiAnimation* source = scene->mAnimations[a];
    AnimationClip clip;
    clip.name = source->mName.C_Str();
    const double ticks = source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 25.0;
    clip.duration = static_cast<float>(source->mDuration / ticks);
    for (unsigned c = 0; c < source->mNumChannels; c++) {
      const aiNodeAnim* channel_source = source->mChannels[c];
      auto it = node_by_name.find(channel_source->mNodeName.C_Str());
      if (it == node_by_name.end()) continue;
      AnimationChannel channel;
      channel.node = it->second;
      const bool top = top_nodes.contains(channel.node);
      const ModelNode& rest = model->nodes_[channel.node];
      for (unsigned k = 0; k < channel_source->mNumPositionKeys; k++) {
        const auto& key = channel_source->mPositionKeys[k];
        glm::vec3 p(key.mValue.x, key.mValue.y, key.mValue.z);
        if (top) p = glm::vec3(axis * glm::vec4(p, 1.0f));
        p *= scale;
        if (unity) p = FlipPosition(p);
        channel.position_times.push_back(static_cast<float>(key.mTime / ticks));
        channel.positions.push_back(p);
      }
      for (unsigned k = 0; k < channel_source->mNumRotationKeys; k++) {
        const auto& key = channel_source->mRotationKeys[k];
        glm::quat q(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
        if (top) {
          glm::vec3 ap, as;
          glm::quat aq;
          Decompose(axis, ap, aq, as);
          q = aq * q;
        }
        if (unity) q = FlipRotation(q);
        channel.rotation_times.push_back(static_cast<float>(key.mTime / ticks));
        channel.rotations.push_back(glm::normalize(q));
      }
      for (unsigned k = 0; k < channel_source->mNumScalingKeys; k++) {
        const auto& key = channel_source->mScalingKeys[k];
        channel.scale_times.push_back(static_cast<float>(key.mTime / ticks));
        channel.scales.push_back(glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z));
      }
      (void)rest;
      clip.channels.push_back(std::move(channel));
    }
    model->clips_.push_back(std::move(clip));
  }

  // ---- Bind pose and bounds
  std::vector<glm::mat4> locals(model->nodes_.size());
  for (size_t i = 0; i < model->nodes_.size(); i++) locals[i] = model->nodes_[i].LocalMatrix();
  model->ComputeGlobals(locals, model->bind_globals_);
  bool first = true;
  for (const auto& mesh : model->meshes_) {
    const glm::mat4& m = model->bind_globals_[mesh.node];
    const glm::vec3 lo = mesh.geometry->bounds_min, hi = mesh.geometry->bounds_max;
    for (int corner = 0; corner < 8; corner++) {
      const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                        (corner & 4) ? hi.z : lo.z);
      const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
      model->bounds_min_ = first ? w : glm::min(model->bounds_min_, w);
      model->bounds_max_ = first ? w : glm::max(model->bounds_max_, w);
      first = false;
    }
  }
  return model;
}

void Model::ComputeGlobals(const std::vector<glm::mat4>& locals,
                           std::vector<glm::mat4>& globals) const {
  globals.resize(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); i++) {
    const int parent = nodes_[i].parent;
    // With the Unity convention the root's own transform belongs to the
    // object that draws the model (it is the Unity GameObject's transform).
    if (parent < 0) {
      globals[i] = root_on_object_ ? glm::mat4(1.0f) : locals[i];
    } else {
      globals[i] = globals[parent] * locals[i];
    }
  }
}

int Model::FindNode(std::string_view name) const {
  for (size_t i = 0; i < nodes_.size(); i++) {
    if (nodes_[i].name == name) return static_cast<int>(i);
  }
  // "Name#3" is the third node called Name, for the many models whose nodes
  // share names (an .obj's groups are often all "Default").
  const size_t hash = name.rfind('#');
  if (hash == std::string_view::npos || hash + 1 >= name.size()) return -1;
  size_t wanted = 0;
  for (char c : name.substr(hash + 1)) {
    if (c < '0' || c > '9') return -1;
    wanted = wanted * 10 + static_cast<size_t>(c - '0');
  }
  const std::string_view base = name.substr(0, hash);
  size_t seen = 0;
  for (size_t i = 0; i < nodes_.size(); i++) {
    if (nodes_[i].name == base && ++seen == wanted) return static_cast<int>(i);
  }
  return -1;
}

const AnimationClip* Model::FindClip(std::string_view name) const {
  for (const auto& clip : clips_) {
    if (clip.name == name) return &clip;
  }
  for (const auto& clip : clips_) {
    const auto bar = clip.name.rfind('|');
    if (bar != std::string::npos && std::string_view(clip.name).substr(bar + 1) == name) {
      return &clip;
    }
  }
  return nullptr;
}

void Model::CollectTriangles(int node, bool include_children,
                             std::vector<glm::vec3>& vertices,
                             std::vector<uint32_t>& indices) const {
  if (node < 0 || node >= static_cast<int>(nodes_.size())) return;
  const glm::mat4 base_inverse = glm::inverse(bind_globals_[node]);
  std::function<void(int)> visit = [&](int index) {
    const glm::mat4 to_node = base_inverse * bind_globals_[index];
    for (int mesh_index : nodes_[index].meshes) {
      const auto& mesh = meshes_[mesh_index];
      const uint32_t base = static_cast<uint32_t>(vertices.size());
      for (const auto& v : mesh.geometry->vertices) {
        vertices.push_back(glm::vec3(to_node * glm::vec4(v.position, 1.0f)));
      }
      for (uint32_t i : mesh.geometry->indices) indices.push_back(base + i);
    }
    if (include_children) {
      for (int child : nodes_[index].children) visit(child);
    }
  };
  visit(node);
}

}  // namespace Mercury

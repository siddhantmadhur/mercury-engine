#include "model_cache.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <type_traits>
#include <vector>

#include "algorithms/parallel_for.h"
#include "debugger/debugger.h"
#include "os/filesystem.h"

namespace Mercury {

namespace {

namespace fs = std::filesystem;

// Bump when what is written, or how Model::Import builds a model, changes.
constexpr uint32_t kVersion = 1;
constexpr char kMagic[4] = {'M', 'M', 'C', 'F'};

struct Hash {
  uint64_t value = 1469598103934665603ull;
  void Bytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
      value ^= bytes[i];
      value *= 1099511628211ull;
    }
  }
  template <typename T>
  void Add(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    Bytes(&v, sizeof(v));
  }
  void Add(const std::string& s) {
    Add(s.size());
    Bytes(s.data(), s.size());
  }
};

// Size and modification time, so an edited file is noticed.
void HashFile(Hash& hash, const fs::path& file) {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  hash.Add(ec ? uint64_t{0} : static_cast<uint64_t>(size));
  const auto time = fs::last_write_time(file, ec);
  hash.Add(ec ? int64_t{0} : static_cast<int64_t>(time.time_since_epoch().count()));
}

// What the cached model depends on: the file, the .mtl files beside an .obj,
// the import settings and the format.
uint64_t CacheKey(const std::string& path, const ModelImportSettings& settings) {
  Hash hash;
  hash.Add(kVersion);
  hash.Add(sizeof(ModelVertex));
  hash.Add(path);
  HashFile(hash, path);
  const fs::path file(path);
  std::string extension = file.extension().string();
  for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (extension == ".obj") {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(file.parent_path(), ec)) {
      std::string other = entry.path().extension().string();
      for (char& c : other) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (other == ".mtl") {
        hash.Add(entry.path().filename().string());
        HashFile(hash, entry.path());
      }
    }
  }
  hash.Add(static_cast<int>(settings.convention));
  hash.Add(settings.scale);
  hash.Add(settings.use_file_scale);
  hash.Add(settings.max_texture_size);
  for (const auto& entry : settings.materials) {
    hash.Add(entry.name);
    hash.Add(entry.color.has_value());
    if (entry.color) hash.Add(*entry.color);
    hash.Add(entry.metallic.value_or(-1.0f));
    hash.Add(entry.smoothness.value_or(-1.0f));
    hash.Add(entry.emission.has_value());
    if (entry.emission) hash.Add(*entry.emission);
  }
  return hash.value;
}

bool Enabled() {
  const char* value = std::getenv("MERCURY_MODEL_CACHE");
  return !value || std::string(value) != "0";
}

fs::path CacheFile(const std::string& path) {
  const fs::path dir = os::CacheDir();
  if (dir.empty()) return {};
  Hash hash;
  hash.Add(fs::absolute(path).lexically_normal().string());
  return dir / "models" /
         std::format("{}-{:016x}.mcache", fs::path(path).stem().string(), hash.value);
}

class Writer {
 public:
  explicit Writer(FILE* file) : file_(file) {}
  bool ok() const { return ok_; }
  void Bytes(const void* data, size_t size) {
    if (ok_ && size > 0) ok_ = std::fwrite(data, 1, size, file_) == size;
  }
  template <typename T>
  void Pod(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    Bytes(&v, sizeof(v));
  }
  template <typename T>
  void Vector(const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    Pod(static_cast<uint64_t>(v.size()));
    Bytes(v.data(), v.size() * sizeof(T));
  }
  void String(const std::string& s) {
    Pod(static_cast<uint64_t>(s.size()));
    Bytes(s.data(), s.size());
  }

 private:
  FILE* file_;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(FILE* file, uint64_t size) : file_(file), left_(size) {}
  bool ok() const { return ok_; }
  void Bytes(void* data, size_t size) {
    if (!ok_ || size == 0) return;
    if (size > left_ || std::fread(data, 1, size, file_) != size) {
      ok_ = false;
      return;
    }
    left_ -= size;
  }
  template <typename T>
  T Pod() {
    static_assert(std::is_trivially_copyable_v<T>);
    T v{};
    Bytes(&v, sizeof(v));
    return v;
  }
  template <typename T>
  void Vector(std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const uint64_t count = Pod<uint64_t>();
    // A count past the end of the file is a damaged file.
    if (!ok_ || count > left_ / std::max<size_t>(sizeof(T), 1)) {
      ok_ = false;
      return;
    }
    v.resize(count);
    Bytes(v.data(), count * sizeof(T));
  }
  std::string String() {
    const uint64_t size = Pod<uint64_t>();
    if (!ok_ || size > left_) {
      ok_ = false;
      return {};
    }
    std::string s(size, '\0');
    Bytes(s.data(), size);
    return s;
  }

 private:
  FILE* file_;
  uint64_t left_;
  bool ok_ = true;
};

}  // namespace

std::shared_ptr<Model> ModelCache::Read(const std::string& path,
                                        const ModelImportSettings& settings,
                                        std::atomic<float>* progress) {
  if (!Enabled() || os::IsPacked(path)) return nullptr;
  const fs::path cache = CacheFile(path);
  std::error_code ec;
  if (cache.empty() || !fs::exists(cache, ec)) return nullptr;
  const auto start = std::chrono::steady_clock::now();
  FILE* file = std::fopen(cache.string().c_str(), "rb");
  if (!file) return nullptr;
  Reader in(file, fs::file_size(cache, ec));
  char magic[4] = {};
  in.Bytes(magic, sizeof(magic));
  const uint64_t key = in.Pod<uint64_t>();
  if (!in.ok() || std::memcmp(magic, kMagic, sizeof(magic)) != 0 ||
      key != CacheKey(path, settings)) {
    std::fclose(file);
    return nullptr;  // stale: imported again and rewritten
  }

  auto model = std::make_shared<Model>();
  model->path_ = path;
  model->root_on_object_ = in.Pod<uint8_t>() != 0;
  model->nodes_.resize(in.Pod<uint64_t>());
  for (auto& node : model->nodes_) {
    if (!in.ok()) break;
    node.name = in.String();
    node.parent = in.Pod<int32_t>();
    in.Vector(node.children);
    in.Vector(node.meshes);
    node.position = in.Pod<glm::vec3>();
    node.rotation = in.Pod<glm::quat>();
    node.scale = in.Pod<glm::vec3>();
  }
  const uint64_t material_count = in.Pod<uint64_t>();
  if (in.ok()) {
    model->materials_.resize(material_count);
    model->material_sources_.resize(material_count);
  }
  for (size_t m = 0; in.ok() && m < material_count; m++) {
    ModelMaterial& material = model->materials_[m];
    material.name = in.String();
    material.color = in.Pod<glm::vec4>();
    material.metallic = in.Pod<float>();
    material.smoothness = in.Pod<float>();
    material.emission = in.Pod<glm::vec3>();
    material.normal_scale = in.Pod<float>();
    material.transparent = in.Pod<uint8_t>() != 0;
    Model::MaterialSources& sources = model->material_sources_[m];
    for (Model::TextureSource* source : {&sources.texture, &sources.emission, &sources.normal}) {
      source->reference = in.String();
      source->use = static_cast<ModelTextureUse>(in.Pod<uint8_t>());
      source->bump_scale = in.Pod<float>();
    }
  }
  const uint64_t mesh_count = in.Pod<uint64_t>();
  if (in.ok()) model->meshes_.resize(mesh_count);
  for (size_t i = 0; in.ok() && i < mesh_count; i++) {
    ModelMesh& mesh = model->meshes_[i];
    mesh.name = in.String();
    mesh.node = in.Pod<int32_t>();
    mesh.material = in.Pod<int32_t>();
    in.Vector(mesh.bones);
    auto geometry = std::make_shared<MeshGeometry>();
    in.Vector(geometry->vertices);
    in.Vector(geometry->indices);
    in.Vector(geometry->clusters);
    geometry->bounds_min = in.Pod<glm::vec3>();
    geometry->bounds_max = in.Pod<glm::vec3>();
    geometry->skinned = in.Pod<uint8_t>() != 0;
    geometry->id = ImageData::NextId();
    mesh.geometry = std::move(geometry);
  }
  const uint64_t clip_count = in.Pod<uint64_t>();
  if (in.ok()) model->clips_.resize(clip_count);
  for (size_t c = 0; in.ok() && c < clip_count; c++) {
    AnimationClip& clip = model->clips_[c];
    clip.name = in.String();
    clip.duration = in.Pod<float>();
    clip.channels.resize(in.Pod<uint64_t>());
    for (auto& channel : clip.channels) {
      if (!in.ok()) break;
      channel.node = in.Pod<int32_t>();
      in.Vector(channel.position_times);
      in.Vector(channel.positions);
      in.Vector(channel.rotation_times);
      in.Vector(channel.rotations);
      in.Vector(channel.scale_times);
      in.Vector(channel.scales);
    }
  }
  std::fclose(file);
  if (!in.ok()) {
    PDebug::warn("{}: damaged model cache {}; importing again", path, cache.string());
    return nullptr;
  }
  // Indices into the arrays must be in range, or the renderer reads past
  // them.
  const int node_count = static_cast<int>(model->nodes_.size());
  for (const auto& node : model->nodes_) {
    if (node.parent >= node_count) return nullptr;
    for (int mesh : node.meshes) {
      if (mesh < 0 || mesh >= static_cast<int>(model->meshes_.size())) return nullptr;
    }
  }
  for (const auto& mesh : model->meshes_) {
    if (mesh.node < 0 || mesh.node >= node_count || mesh.material < 0 ||
        mesh.material >= static_cast<int>(model->materials_.size())) {
      return nullptr;
    }
    const size_t vertices = mesh.geometry->vertices.size();
    for (uint32_t index : mesh.geometry->indices) {
      if (index >= vertices) return nullptr;
    }
  }
  const double read_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (progress) progress->store(0.5f);

  // The textures, from their files, each once.
  using Key = std::pair<std::string, ModelTextureUse>;
  std::map<Key, std::shared_ptr<ImageData>> textures;
  std::vector<const Model::TextureSource*> wanted;
  for (const auto& sources : model->material_sources_) {
    for (const auto* source : {&sources.texture, &sources.emission, &sources.normal}) {
      if (!source->reference.empty() &&
          textures.emplace(Key(source->reference, source->use), nullptr).second) {
        wanted.push_back(source);
      }
    }
  }
  std::vector<std::shared_ptr<ImageData>> decoded(wanted.size());
  std::atomic<size_t> done{0};
  ParallelFor(wanted.size(), [&](size_t i) {
    decoded[i] = Model::DecodeTexture(path, *wanted[i], settings);
    if (progress) {
      progress->store(0.5f + 0.5f * static_cast<float>(++done) / static_cast<float>(wanted.size()));
    }
  });
  for (size_t i = 0; i < wanted.size(); i++) {
    textures[Key(wanted[i]->reference, wanted[i]->use)] = std::move(decoded[i]);
  }
  auto texture = [&](const Model::TextureSource& source) -> std::shared_ptr<ImageData> {
    if (source.reference.empty()) return nullptr;
    return textures[Key(source.reference, source.use)];
  };
  for (size_t m = 0; m < model->materials_.size(); m++) {
    const auto& sources = model->material_sources_[m];
    model->materials_[m].texture = texture(sources.texture);
    model->materials_[m].emission_texture = texture(sources.emission);
    model->materials_[m].normal_texture = texture(sources.normal);
  }
  model->Finish();
  if (progress) progress->store(1.0f);
  PDebug::log("Loaded {} from the cache in {:.2f}s (meshes {:.2f}s)", path,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
              read_seconds);
  return model;
}

void ModelCache::Write(const Model& model, const std::string& path,
                       const ModelImportSettings& settings) {
  if (!Enabled() || model.embedded_textures_ || os::IsPacked(path)) return;
  const fs::path cache = CacheFile(path);
  if (cache.empty()) return;
  std::error_code ec;
  fs::create_directories(cache.parent_path(), ec);
  // Written beside it and renamed over it, so a reader never sees half.
  const fs::path temporary = cache.string() + ".tmp";
  FILE* file = std::fopen(temporary.string().c_str(), "wb");
  if (!file) return;
  Writer out(file);
  out.Bytes(kMagic, sizeof(kMagic));
  out.Pod(CacheKey(path, settings));
  out.Pod<uint8_t>(model.root_on_object_ ? 1 : 0);
  out.Pod(static_cast<uint64_t>(model.nodes_.size()));
  for (const auto& node : model.nodes_) {
    out.String(node.name);
    out.Pod<int32_t>(node.parent);
    out.Vector(node.children);
    out.Vector(node.meshes);
    out.Pod(node.position);
    out.Pod(node.rotation);
    out.Pod(node.scale);
  }
  out.Pod(static_cast<uint64_t>(model.materials_.size()));
  for (size_t m = 0; m < model.materials_.size(); m++) {
    const ModelMaterial& material = model.materials_[m];
    out.String(material.name);
    out.Pod(material.color);
    out.Pod(material.metallic);
    out.Pod(material.smoothness);
    out.Pod(material.emission);
    out.Pod(material.normal_scale);
    out.Pod<uint8_t>(material.transparent ? 1 : 0);
    const Model::MaterialSources sources =
        m < model.material_sources_.size() ? model.material_sources_[m] : Model::MaterialSources{};
    for (const Model::TextureSource* source :
         {&sources.texture, &sources.emission, &sources.normal}) {
      out.String(source->reference);
      out.Pod(static_cast<uint8_t>(source->use));
      out.Pod(source->bump_scale);
    }
  }
  out.Pod(static_cast<uint64_t>(model.meshes_.size()));
  for (const auto& mesh : model.meshes_) {
    out.String(mesh.name);
    out.Pod<int32_t>(mesh.node);
    out.Pod<int32_t>(mesh.material);
    out.Vector(mesh.bones);
    out.Vector(mesh.geometry->vertices);
    out.Vector(mesh.geometry->indices);
    out.Vector(mesh.geometry->clusters);
    out.Pod(mesh.geometry->bounds_min);
    out.Pod(mesh.geometry->bounds_max);
    out.Pod<uint8_t>(mesh.geometry->skinned ? 1 : 0);
  }
  out.Pod(static_cast<uint64_t>(model.clips_.size()));
  for (const auto& clip : model.clips_) {
    out.String(clip.name);
    out.Pod(clip.duration);
    out.Pod(static_cast<uint64_t>(clip.channels.size()));
    for (const auto& channel : clip.channels) {
      out.Pod<int32_t>(channel.node);
      out.Vector(channel.position_times);
      out.Vector(channel.positions);
      out.Vector(channel.rotation_times);
      out.Vector(channel.rotations);
      out.Vector(channel.scale_times);
      out.Vector(channel.scales);
    }
  }
  bool written = out.ok();
  if (std::fclose(file) != 0) written = false;
  if (!written) {
    fs::remove(temporary, ec);
    PDebug::warn("{}: could not write the model cache {}", path, cache.string());
    return;
  }
  fs::rename(temporary, cache, ec);
  if (ec) fs::remove(temporary, ec);
}

}  // namespace Mercury

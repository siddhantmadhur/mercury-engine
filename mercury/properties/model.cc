#include "model.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <set>
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "properties/texture2d.h"
#include "properties/vfs_io_system.h"
#include "serialization/mtext.h"

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
  }
  return settings;
}

// ----------------------------------------------------------------- Model ---

namespace {

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

// Finds a texture a model refers to, the way Unity's importer searches:
// next to the model, in a "Textures" folder, then in the folders above.
std::string FindTexture(const fs::path& model_dir, const std::string& reference) {
  std::string name = reference;
  std::replace(name.begin(), name.end(), '\\', '/');
  const fs::path file = fs::path(name).filename();
  if (file.empty()) return "";
  std::error_code ec;
  // A relative path as written in the file.
  if (!fs::path(name).is_absolute()) {
    const fs::path direct = model_dir / name;
    if (fs::exists(direct, ec)) return direct.string();
  }
  fs::path dir = model_dir;
  for (int depth = 0; depth < 6 && !dir.empty(); depth++) {
    for (const char* sub : {"", "Textures", "textures", "Texture", "texture"}) {
      const fs::path candidate = dir / sub / file;
      if (fs::exists(candidate, ec)) return candidate.string();
    }
    if (dir == dir.parent_path()) break;
    dir = dir.parent_path();
  }
  return "";
}

}  // namespace

std::shared_ptr<Model> Model::Load(const std::string& path,
                                   const ModelImportSettings& settings,
                                   std::string* error) {
  Assimp::Importer importer;
  UseVfs(importer);
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
  importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                              aiPrimitiveType_POINT | aiPrimitiveType_LINE);
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs |
                aiProcess_LimitBoneWeights | aiProcess_SortByPType |
                aiProcess_ValidateDataStructure);
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
  std::map<std::string, std::shared_ptr<ImageData>> loaded_textures;
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
    aiString texture_path;
    if (source->GetTexture(aiTextureType_BASE_COLOR, 0, &texture_path) != AI_SUCCESS) {
      source->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path);
    }
    const std::string reference = texture_path.C_Str();
    if (!reference.empty()) {
      if (auto it = loaded_textures.find(reference); it != loaded_textures.end()) {
        material.texture = it->second;
      } else {
        std::shared_ptr<ImageData> image;
        if (const aiTexture* embedded = scene->GetEmbeddedTexture(reference.c_str())) {
          if (embedded->mHeight == 0) {
            image = ImageData::FromMemory(reinterpret_cast<const uint8_t*>(embedded->pcData),
                                          embedded->mWidth, path + ":" + reference);
          } else {
            std::vector<uint8_t> pixels(size_t(embedded->mWidth) * embedded->mHeight * 4);
            for (size_t i = 0; i < size_t(embedded->mWidth) * embedded->mHeight; i++) {
              pixels[i * 4 + 0] = embedded->pcData[i].r;
              pixels[i * 4 + 1] = embedded->pcData[i].g;
              pixels[i * 4 + 2] = embedded->pcData[i].b;
              pixels[i * 4 + 3] = embedded->pcData[i].a;
            }
            image = ImageData::FromPixels(embedded->mWidth, embedded->mHeight,
                                          std::move(pixels), path + ":" + reference);
          }
        } else if (auto file = FindTexture(model_dir, reference); !file.empty()) {
          image = ImageData::Load(file);
        }
        if (!image) PDebug::warn("{}: texture {} not found", path, reference);
        loaded_textures[reference] = image;
        material.texture = image;
      }
    }
    model->materials_.push_back(std::move(material));
  }
  if (model->materials_.empty()) model->materials_.push_back(ModelMaterial{});

  // ---- Meshes
  for (const auto& [source_node, index] : node_order) {
    for (unsigned i = 0; i < source_node->mNumMeshes; i++) {
      const aiMesh* source = scene->mMeshes[source_node->mMeshes[i]];
      if (!(source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;
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
      const int mesh_index = static_cast<int>(model->meshes_.size());
      model->meshes_.push_back(std::move(mesh));
      model->nodes_[index].meshes.push_back(mesh_index);
    }
  }

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

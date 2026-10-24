#include "parallel_obj.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/IOSystem.hpp>
#include <assimp/MemoryIOWrapper.h>
#include <assimp/material.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algorithms/parallel_for.h"
#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "properties/vfs_io_system.h"

namespace Mercury {

namespace {

namespace fs = std::filesystem;

constexpr size_t kMinPieceBytes = 4 * 1024 * 1024;

bool IsBlank(char c) { return c == ' ' || c == '\t'; }
bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// What a line of the file is, as far as cutting it up is concerned.
enum class Line {
  kOther,
  kVertex,
  kTexCoord,
  kNormal,
  kIndexed,      // f, l and p: the lines that refer to vertices
  kObject,       // o and g
  kUseMaterial,
  kMaterialLib,
  kUnsupported,  // freeform curves and surfaces refer to vertices too
};

// The end of the line starting at `start`: its '\n', or `end`. A backslash
// before the line break continues the line, as Assimp reads it.
const char* LineEnd(const char* start, const char* end) {
  const char* p = start;
  while (p < end) {
    const char* newline = static_cast<const char*>(std::memchr(p, '\n', end - p));
    if (!newline) return end;
    const char* last = newline;
    if (last > start && last[-1] == '\r') last--;
    if (last > start && last[-1] == '\\') {
      p = newline + 1;
      continue;
    }
    return newline;
  }
  return end;
}

bool StartsWith(const char* p, const char* end, std::string_view word) {
  return static_cast<size_t>(end - p) > word.size() &&
         std::memcmp(p, word.data(), word.size()) == 0 && IsBlank(p[word.size()]);
}

// Classifies the line [p, end); `after` is left just past its keyword.
Line Classify(const char* p, const char* end, const char*& after) {
  while (p < end && IsBlank(*p)) p++;
  after = p;
  if (p >= end) return Line::kOther;
  const char c = *p;
  const bool next_blank = p + 1 < end && IsBlank(p[1]);
  switch (c) {
    case 'v':
      if (next_blank) {
        after = p + 1;
        return Line::kVertex;
      }
      if (p + 2 < end && IsBlank(p[2])) {
        if (p[1] == 't') {
          after = p + 2;
          return Line::kTexCoord;
        }
        if (p[1] == 'n') {
          after = p + 2;
          return Line::kNormal;
        }
      }
      return Line::kOther;
    case 'f':
    case 'l':
    case 'p':
      if (!next_blank) return Line::kOther;
      after = p + 1;
      return Line::kIndexed;
    case 'o':
    case 'g':
      if (!next_blank) return Line::kOther;
      after = p + 1;
      return Line::kObject;
    case 'u':
      if (!StartsWith(p, end, "usemtl")) return Line::kOther;
      after = p + 6;
      return Line::kUseMaterial;
    case 'm':
      if (!StartsWith(p, end, "mtllib")) return Line::kOther;
      after = p + 6;
      return Line::kMaterialLib;
    case 'c':
      return StartsWith(p, end, "cstype") || StartsWith(p, end, "curv") ? Line::kUnsupported
                                                                         : Line::kOther;
    case 's':
      return StartsWith(p, end, "surf") ? Line::kUnsupported : Line::kOther;
    default:
      return Line::kOther;
  }
}

std::string Trimmed(const char* p, const char* end) {
  while (p < end && IsSpace(*p)) p++;
  while (end > p && IsSpace(end[-1])) end--;
  return std::string(p, end);
}

// What an object's name is for telling objects apart: its first word.
std::string FirstWord(const char* p, const char* end) {
  while (p < end && IsSpace(*p)) p++;
  const char* word = p;
  while (p < end && !IsSpace(*p)) p++;
  return std::string(word, p);
}

// ---------------------------------------------------------------- Scanning ---

// The lowest and highest vertex, texture coordinate and normal index that
// some face lines use.
struct RefRange {
  uint32_t lo[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
  uint32_t hi[3] = {0, 0, 0};
  void Merge(const RefRange& other) {
    for (int k = 0; k < 3; k++) {
      lo[k] = std::min(lo[k], other.lo[k]);
      hi[k] = std::max(hi[k], other.hi[k]);
    }
  }
};

// Notes the indices of a face, line or point line. False for relative
// (negative) indices, which cannot be moved to a piece.
bool NoteIndices(const char* p, const char* end, RefRange& range) {
  int position = 0;
  while (p < end && *p != '#') {
    const char c = *p;
    if (c == '/') {
      position++;
      p++;
    } else if (IsSpace(c)) {
      position = 0;
      p++;
    } else if (c == '-') {
      return false;
    } else if (c >= '0' && c <= '9') {
      uint64_t value = 0;
      while (p < end && *p >= '0' && *p <= '9') {
        value = std::min<uint64_t>(value * 10 + static_cast<uint64_t>(*p - '0'), UINT32_MAX);
        p++;
      }
      if (position <= 2 && value > 0) {
        range.lo[position] = std::min(range.lo[position], static_cast<uint32_t>(value));
        range.hi[position] = std::max(range.hi[position], static_cast<uint32_t>(value));
      }
    } else {
      p++;
    }
  }
  return true;
}

struct Event {
  Line kind;
  size_t offset;     // where its line starts
  std::string text;  // an object's first word; the material or library name
  uint32_t seen[3];  // vertex, texture coordinate and normal lines before it
  bool faces_before = false;  // objects: a face line since the previous object
  bool group = false;         // objects: a "g" line rather than an "o" line
  RefRange refs;              // objects: what the face lines after it refer to
};

struct RangeScan {
  std::vector<Event> events;
  uint32_t counts[3] = {0, 0, 0};
  bool faces_since_object = false;  // a face line after the last object (or the start)
  bool has_object = false;
  RefRange head;  // what the face lines before the first object refer to
  bool unsupported = false;
};

void ScanRange(const char* base, size_t begin, size_t end, RangeScan& scan) {
  const char* p = base + begin;
  const char* stop = base + end;
  if (begin == 0 && end >= 3 && static_cast<uint8_t>(p[0]) == 0xEF &&
      static_cast<uint8_t>(p[1]) == 0xBB && static_cast<uint8_t>(p[2]) == 0xBF) {
    p += 3;  // byte order mark
  }
  size_t last_object = 0;  // index in scan.events of the latest object line
  while (p < stop) {
    const char* line_end = LineEnd(p, stop);
    const char* after = p;
    const Line kind = Classify(p, line_end, after);
    switch (kind) {
      case Line::kVertex: scan.counts[0]++; break;
      case Line::kTexCoord: scan.counts[1]++; break;
      case Line::kNormal: scan.counts[2]++; break;
      case Line::kIndexed: {
        scan.faces_since_object = true;
        RefRange& refs = scan.has_object ? scan.events[last_object].refs : scan.head;
        if (!NoteIndices(after, line_end, refs)) {
          scan.unsupported = true;
          return;
        }
        break;
      }
      case Line::kUnsupported: scan.unsupported = true; return;
      case Line::kObject:
      case Line::kUseMaterial:
      case Line::kMaterialLib: {
        std::string text = kind == Line::kObject ? FirstWord(after, line_end)
                                                 : Trimmed(after, line_end);
        // Lines without a name change nothing in Assimp.
        if (text.empty()) break;
        Event event{kind, static_cast<size_t>(p - base), std::move(text),
                    {scan.counts[0], scan.counts[1], scan.counts[2]}};
        if (kind == Line::kObject) {
          event.group = after[-1] == 'g';
          event.faces_before = scan.faces_since_object;
          scan.faces_since_object = false;
          scan.has_object = true;
          last_object = scan.events.size();
        }
        scan.events.push_back(std::move(event));
        break;
      }
      case Line::kOther: break;
    }
    p = line_end + 1;
  }
}

// A cut of the file: a run of whole objects that is imported as its own file.
struct Piece {
  size_t begin = 0, end = 0;
  uint32_t first[3] = {0, 0, 0};  // vertex, texture coordinate and normal lines before it
  uint32_t total[3] = {0, 0, 0};  // ...and in it
  std::string material;           // the one in use where it starts
  std::vector<std::string> libraries;
};

// ------------------------------------------------------------- Rewriting ---

// Copies a face, line or point line, moving its indices down by what the
// piece leaves out. False if an index would leave the piece, or cannot be
// moved.
bool RewriteIndexLine(const char* p, const char* end, const Piece& piece,
                      const uint32_t seen[3], std::string& out) {
  const bool global_quirk = piece.first[1] + seen[1] == 0 && piece.first[2] + seen[2] > 0;
  const bool local_quirk = seen[1] == 0 && seen[2] > 0;
  int position = 0;
  while (p < end) {
    const char c = *p;
    if (c == '#') {
      out.append(p, end - p);
      return true;
    }
    if (c == '/') {
      position++;
      out.push_back(c);
      p++;
    } else if (IsSpace(c)) {
      position = 0;
      out.push_back(c);
      p++;
    } else if (c == '-') {
      return false;  // relative to the vertices read so far
    } else if (c >= '0' && c <= '9') {
      uint64_t value = 0;
      while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + static_cast<uint64_t>(*p - '0');
        p++;
        if (value > UINT32_MAX) return false;
      }
      int kind = position;
      if (position == 1 && piece.first[1] + seen[1] == 0 && piece.first[2] + seen[2] > 0) {
        // Assimp reads "v/n" as a normal when the file has no texture
        // coordinates; the piece must see the same.
        if (global_quirk != local_quirk) return false;
        kind = 2;
        position = 2;
      }
      if (kind > 2 || value == 0) return false;
      if (value <= piece.first[kind] || value - piece.first[kind] > piece.total[kind]) {
        return false;
      }
      char digits[16];
      const auto result = std::to_chars(digits, digits + sizeof(digits),
                                        static_cast<uint32_t>(value - piece.first[kind]));
      out.append(digits, result.ptr - digits);
    } else {
      out.push_back(c);
      p++;
    }
  }
  return true;
}

// The text of a piece as a file of its own. False if it cannot be one.
bool BuildPiece(const char* base, const Piece& piece, bool first, std::string& out) {
  out.reserve(piece.end - piece.begin + 4096);
  for (const std::string& library : piece.libraries) {
    out += "mtllib " + library + "\n";
  }
  const char* p = base + piece.begin;
  const char* stop = base + piece.end;
  uint32_t seen[3] = {0, 0, 0};
  if (!first) {
    // The object's own line, then the material it would have inherited.
    const char* line_end = LineEnd(p, stop);
    out.append(p, std::min(line_end + 1, stop) - p);
    if (!piece.material.empty()) out += "usemtl " + piece.material + "\n";
    p = std::min(line_end + 1, stop);
  }
  while (p < stop) {
    const char* line_end = LineEnd(p, stop);
    const char* after = p;
    switch (Classify(p, line_end, after)) {
      case Line::kVertex: seen[0]++; break;
      case Line::kTexCoord: seen[1]++; break;
      case Line::kNormal: seen[2]++; break;
      case Line::kIndexed:
        out.append(p, after - p);
        if (!RewriteIndexLine(after, line_end, piece, seen, out)) return false;
        if (line_end < stop) out.push_back('\n');
        p = line_end + 1;
        continue;
      case Line::kUnsupported: return false;
      default: break;
    }
    out.append(p, std::min(line_end + 1, stop) - p);
    p = line_end + 1;
  }
  return true;
}

// Serves one piece from memory under a made-up file name beside the real
// one, so its material library is found as it would be for the real file;
// everything else comes from the file system.
class PieceIOSystem : public Assimp::IOSystem {
 public:
  PieceIOSystem(std::string name, const std::string* text)
      : name_(std::move(name)), text_(text) {
    if (os::HasMounts()) {
      base_ = std::make_unique<VfsIOSystem>();
    } else {
      base_ = std::make_unique<Assimp::DefaultIOSystem>();
    }
  }

  bool Exists(const char* file) const override {
    return name_ == file || base_->Exists(file);
  }
  char getOsSeparator() const override { return base_->getOsSeparator(); }
  Assimp::IOStream* Open(const char* file, const char* mode) override {
    if (name_ == file) {
      return new Assimp::MemoryIOStream(reinterpret_cast<const uint8_t*>(text_->data()),
                                        text_->size(), false);
    }
    return base_->Open(file, mode);
  }
  void Close(Assimp::IOStream* stream) override {
    if (dynamic_cast<Assimp::MemoryIOStream*>(stream)) {
      delete stream;
    } else {
      base_->Close(stream);
    }
  }

 private:
  std::string name_;
  const std::string* text_;
  std::unique_ptr<Assimp::IOSystem> base_;
};

// ----------------------------------------------------------------- Joining ---

std::string MaterialName(const aiMaterial* material) {
  aiString name;
  if (material && material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) return name.C_Str();
  return std::string();
}

void OffsetMeshIndices(aiNode* node, unsigned offset) {
  for (unsigned i = 0; i < node->mNumMeshes; i++) node->mMeshes[i] += offset;
  for (unsigned i = 0; i < node->mNumChildren; i++) OffsetMeshIndices(node->mChildren[i], offset);
}

// Gives `into` the meshes of `from`, after its own.
void AppendMeshes(aiNode& into, const aiNode& from) {
  if (from.mNumMeshes == 0) return;
  auto* meshes = new unsigned[into.mNumMeshes + from.mNumMeshes];
  std::copy(into.mMeshes, into.mMeshes + into.mNumMeshes, meshes);
  std::copy(from.mMeshes, from.mMeshes + from.mNumMeshes, meshes + into.mNumMeshes);
  delete[] into.mMeshes;
  into.mMeshes = meshes;
  into.mNumMeshes += from.mNumMeshes;
}

// Joins the scenes of the pieces, in order, into the scene the whole file
// would have made. Takes the nodes, meshes and materials out of them.
std::unique_ptr<aiScene> JoinScenes(std::vector<std::unique_ptr<aiScene>>& scenes,
                                    std::vector<std::unordered_set<std::string>>& named,
                                    const std::string& file_name) {
  auto joined = std::make_unique<aiScene>();
  joined->mRootNode = new aiNode;
  joined->mRootNode->mName.Set(file_name);
  if (!scenes.empty() && scenes[0]) joined->mFlags = scenes[0]->mFlags;

  std::vector<aiNode*> children;
  std::vector<aiMesh*> meshes;
  std::vector<aiMaterial*> materials;
  std::unordered_map<std::string, unsigned> material_index;
  std::unordered_map<std::string, aiNode*> node_by_name;  // the first of each name

  for (size_t piece = 0; piece < scenes.size(); piece++) {
    auto& scene = scenes[piece];
    if (!scene || !scene->mRootNode) return nullptr;
    // Materials are told apart by name; the first one seen is kept.
    std::vector<unsigned> remap(scene->mNumMaterials, 0);
    for (unsigned m = 0; m < scene->mNumMaterials; m++) {
      const std::string name = MaterialName(scene->mMaterials[m]);
      auto [it, added] = material_index.try_emplace(name, materials.size());
      if (added) {
        materials.push_back(scene->mMaterials[m]);
        scene->mMaterials[m] = nullptr;
      }
      remap[m] = it->second;
    }
    const unsigned mesh_base = static_cast<unsigned>(meshes.size());
    for (unsigned i = 0; i < scene->mNumMeshes; i++) {
      aiMesh* mesh = scene->mMeshes[i];
      if (mesh->mMaterialIndex >= remap.size()) return nullptr;
      mesh->mMaterialIndex = remap[mesh->mMaterialIndex];
      meshes.push_back(mesh);
      scene->mMeshes[i] = nullptr;
    }
    aiNode* root = scene->mRootNode;
    if (root->mNumMeshes != 0) return nullptr;
    for (unsigned i = 0; i < root->mNumChildren; i++) {
      aiNode* child = root->mChildren[i];
      OffsetMeshIndices(child, mesh_base);
      root->mChildren[i] = nullptr;
      // An "o" line naming an object that exists goes on with that object.
      if (auto name = named[piece].find(child->mName.C_Str()); name != named[piece].end()) {
        named[piece].erase(name);  // only the piece's first node of the name
        if (auto it = node_by_name.find(child->mName.C_Str()); it != node_by_name.end()) {
          AppendMeshes(*it->second, *child);
          delete child;
          continue;
        }
      }
      node_by_name.try_emplace(child->mName.C_Str(), child);
      child->mParent = joined->mRootNode;
      children.push_back(child);
    }
  }

  // Assimp lists the meshes object by object, so an object that continues
  // in a later piece keeps all its meshes together.
  {
    std::vector<aiMesh*> ordered;
    ordered.reserve(meshes.size());
    for (aiNode* child : children) {
      for (unsigned i = 0; i < child->mNumMeshes; i++) {
        const unsigned old_index = child->mMeshes[i];
        child->mMeshes[i] = static_cast<unsigned>(ordered.size());
        ordered.push_back(meshes[old_index]);
        meshes[old_index] = nullptr;
      }
    }
    for (aiMesh* mesh : meshes) {
      if (mesh) ordered.push_back(mesh);
    }
    meshes = std::move(ordered);
  }

  aiNode* root = joined->mRootNode;
  root->mNumChildren = static_cast<unsigned>(children.size());
  root->mChildren = new aiNode*[children.size()];
  std::copy(children.begin(), children.end(), root->mChildren);
  joined->mNumMeshes = static_cast<unsigned>(meshes.size());
  if (!meshes.empty()) {
    joined->mMeshes = new aiMesh*[meshes.size()];
    std::copy(meshes.begin(), meshes.end(), joined->mMeshes);
  }
  joined->mNumMaterials = static_cast<unsigned>(materials.size());
  if (!materials.empty()) {
    joined->mMaterials = new aiMaterial*[materials.size()];
    std::copy(materials.begin(), materials.end(), joined->mMaterials);
  }
  return joined;
}

}  // namespace

std::unique_ptr<aiScene> ImportObjInParallel(const std::string& path,
                                             const ParallelObjOptions& options) {
  if (WorkerThreadCount() < 2) return nullptr;
  std::error_code ec;
  const auto disk_size = fs::file_size(path, ec);
  if (!ec && disk_size < options.min_bytes) return nullptr;
  auto file = os::ReadBytes(path);
  if (!file || file->size() < std::max<size_t>(options.min_bytes, 1)) return nullptr;
  const char* base = reinterpret_cast<const char*>(file->data());
  const size_t size = file->size();

  // ---- Scan the file in ranges, on every core
  const size_t range_count = std::clamp<size_t>(size / (1024 * 1024), 1, WorkerThreadCount() * 4);
  std::vector<size_t> bounds{0};
  for (size_t i = 1; i < range_count; i++) {
    size_t at = std::max(bounds.back(), size * i / range_count);
    // Move to just after a real line break.
    while (at < size) {
      const char* newline = static_cast<const char*>(std::memchr(base + at, '\n', size - at));
      if (!newline) {
        at = size;
        break;
      }
      const char* last = newline;
      if (last > base && last[-1] == '\r') last--;
      at = newline - base + 1;
      if (!(last > base && last[-1] == '\\')) break;
    }
    if (at > bounds.back() && at < size) bounds.push_back(at);
  }
  bounds.push_back(size);
  std::vector<RangeScan> scans(bounds.size() - 1);
  ParallelFor(scans.size(), [&](size_t i) { ScanRange(base, bounds[i], bounds[i + 1], scans[i]); });

  // Join the ranges' events, with counts that run from the start of the file.
  struct Object {
    size_t offset;
    std::string name;
    uint32_t seen[3];
    bool faces_before;
    bool group;
    RefRange refs;
  };
  std::vector<Object> objects;
  RefRange before_objects;  // faces before the first object
  std::vector<Event> others;  // usemtl and mtllib lines, in file order
  uint32_t running[3] = {0, 0, 0};
  bool faces_carried = false;
  for (const RangeScan& scan : scans) {
    if (scan.unsupported) return nullptr;
    (objects.empty() ? before_objects : objects.back().refs).Merge(scan.head);
    bool first_object = true;
    for (const Event& event : scan.events) {
      Event absolute = event;
      for (int k = 0; k < 3; k++) absolute.seen[k] += running[k];
      if (event.kind == Line::kObject) {
        bool faces = event.faces_before;
        if (first_object) faces = faces || faces_carried;
        first_object = false;
        objects.push_back({absolute.offset, absolute.text,
                           {absolute.seen[0], absolute.seen[1], absolute.seen[2]}, faces,
                           event.group, event.refs});
      } else {
        others.push_back(std::move(absolute));
      }
    }
    faces_carried = scan.has_object ? scan.faces_since_object
                                    : faces_carried || scan.faces_since_object;
    for (int k = 0; k < 3; k++) running[k] += scan.counts[k];
  }
  const uint32_t totals[3] = {running[0], running[1], running[2]};
  if (objects.size() < 2) return nullptr;

  // ---- Choose where to cut
  // Where Assimp's state would differ if the file were cut before an object:
  // "g" with the name of the last "g" line is ignored, so a piece must not
  // start by repeating it. ("o" with a name an object already has goes on
  // with that object; the pieces' nodes are joined by name for that.)
  std::vector<std::string> previous_group(objects.size());  // the last "g" before an object
  std::vector<std::string> next_group(objects.size() + 1);  // the first "g" from an object on
  {
    std::string last;
    for (size_t i = 0; i < objects.size(); i++) {
      previous_group[i] = last;
      if (objects[i].group) last = objects[i].name;
    }
    std::string next;
    for (size_t i = objects.size(); i-- > 0;) {
      if (objects[i].group) next = objects[i].name;
      next_group[i] = next;
    }
  }
  // Faces must not reach across a cut: those before it only use vertices
  // before it, and those after it only vertices after it.
  std::vector<RefRange> before_cut(objects.size() + 1);  // faces ahead of object i
  std::vector<RefRange> after_cut(objects.size() + 1);   // faces from object i on
  before_cut[0] = before_objects;
  for (size_t i = 0; i < objects.size(); i++) {
    before_cut[i + 1] = before_cut[i];
    before_cut[i + 1].Merge(objects[i].refs);
  }
  for (size_t i = objects.size(); i-- > 0;) {
    after_cut[i] = after_cut[i + 1];
    after_cut[i].Merge(objects[i].refs);
  }
  auto faces_stay_apart = [&](size_t i) {
    for (int k = 0; k < 3; k++) {
      if (before_cut[i].hi[k] > objects[i].seen[k]) return false;
      if (after_cut[i].lo[k] != UINT32_MAX && after_cut[i].lo[k] <= objects[i].seen[k]) {
        return false;
      }
    }
    return true;
  };
  const size_t pieces_wanted = options.max_pieces
                                   ? options.max_pieces
                                   : std::clamp<size_t>(size / kMinPieceBytes, 2,
                                                        WorkerThreadCount() * 2);
  const size_t target = size / pieces_wanted;
  std::vector<size_t> cuts{0};  // object indices where a piece starts
  size_t piece_start = 0;
  for (size_t i = 1; i < objects.size(); i++) {
    if (!objects[i].faces_before) continue;
    if (!previous_group[i].empty() && next_group[i] == previous_group[i]) continue;
    if (!faces_stay_apart(i)) continue;
    if (objects[i].offset - piece_start >= target) {
      cuts.push_back(i);
      piece_start = objects[i].offset;
    }
  }
  if (cuts.size() < 2) return nullptr;

  std::vector<Piece> pieces(cuts.size());
  size_t other = 0;
  std::vector<std::string> libraries;
  std::string material;
  for (size_t i = 0; i < cuts.size(); i++) {
    Piece& piece = pieces[i];
    piece.begin = i == 0 ? 0 : objects[cuts[i]].offset;
    piece.end = i + 1 < cuts.size() ? objects[cuts[i + 1]].offset : size;
    for (int k = 0; k < 3; k++) {
      piece.first[k] = i == 0 ? 0 : objects[cuts[i]].seen[k];
      const uint32_t after = i + 1 < cuts.size() ? objects[cuts[i + 1]].seen[k] : totals[k];
      piece.total[k] = after - piece.first[k];
    }
    // The materials and libraries named before the piece.
    while (other < others.size() && others[other].offset < piece.begin) {
      if (others[other].kind == Line::kMaterialLib) {
        libraries.push_back(others[other].text);
      } else {
        material = others[other].text;
      }
      other++;
    }
    if (i > 0) {
      piece.libraries = libraries;
      piece.material = material;
    }
  }

  // The "o" names of each piece, for joining its nodes with earlier ones.
  // Only a name whose first object in the piece comes from an "o" line is
  // one that line may have continued from an earlier piece.
  std::vector<std::unordered_set<std::string>> named(pieces.size());
  std::unordered_set<std::string> seen_in_piece;
  for (size_t i = 0, piece = 0; i < objects.size(); i++) {
    if (piece + 1 < cuts.size() && i >= cuts[piece + 1]) {
      piece++;
      seen_in_piece.clear();
    }
    if (seen_in_piece.insert(objects[i].name).second && !objects[i].group) {
      named[piece].insert(objects[i].name);
    }
  }

  // ---- Import the pieces at the same time
  const fs::path file_path(path);
  const std::string stem = file_path.stem().string();
  std::vector<std::unique_ptr<aiScene>> scenes(pieces.size());
  std::atomic<bool> failed{false};
  std::atomic<size_t> finished{0};
  ParallelFor(pieces.size(), [&](size_t i) {
    if (failed) return;
    std::string text;
    if (!BuildPiece(base, pieces[i], i == 0, text)) {
      failed = true;
      return;
    }
    const std::string name =
        (file_path.parent_path() / (stem + ".__piece" + std::to_string(i) + ".obj")).string();
    Assimp::Importer importer;
    importer.SetIOHandler(new PieceIOSystem(name, &text));
    if (options.configure) options.configure(importer);
    const aiScene* scene = importer.ReadFile(name, options.flags);
    if (!scene || !scene->mRootNode) {
      failed = true;
      return;
    }
    scenes[i].reset(importer.GetOrphanedScene());
    if (options.progress) {
      options.progress(static_cast<float>(++finished) / static_cast<float>(pieces.size()));
    }
  });
  if (failed) {
    PDebug::log("{}: could not be cut into pieces, importing it whole", path);
    return nullptr;
  }
  return JoinScenes(scenes, named, file_path.filename().string());
}

}  // namespace Mercury

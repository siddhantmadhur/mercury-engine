// Tests for loading one big model on several threads: an .obj read in pieces
// must give the model the whole file would, and ParallelFor must run
// everything once.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "algorithms/parallel_for.h"
#include "properties/model.h"
#include "properties/parallel_obj.h"

using namespace Mercury;

namespace {

namespace fs = std::filesystem;

const fs::path kModels = fs::path(MERCURY_SOURCE_DIR) / "examples/model_viewer/models";

ModelImportSettings Whole() {
  ModelImportSettings settings;
  settings.parallel_obj_min_bytes = SIZE_MAX;
  settings.max_texture_size = 64;
  return settings;
}

ModelImportSettings InPieces(size_t pieces) {
  ModelImportSettings settings;
  settings.parallel_obj_min_bytes = 1;
  settings.parallel_obj_pieces = pieces;
  settings.max_texture_size = 64;
  return settings;
}

bool CanSplit(const std::string& path, size_t pieces) {
  ParallelObjOptions options;
  options.min_bytes = 1;
  options.max_pieces = pieces;
  options.flags = 0;
  return ImportObjInParallel(path, options) != nullptr;
}

std::string MaterialNameOf(const Model& model, int index) {
  return model.Materials()[index].name;
}

// Same nodes, meshes (in order, with the same material and size) and materials.
// `names` also compares the meshes' names, which differ where an "o" line
// repeats an object's name (Assimp names the continuing mesh after a material).
void ExpectSameModel(const Model& whole, const Model& pieces, bool names = true) {
  ASSERT_EQ(whole.Nodes().size(), pieces.Nodes().size());
  for (size_t i = 0; i < whole.Nodes().size(); i++) {
    EXPECT_EQ(whole.Nodes()[i].name, pieces.Nodes()[i].name) << "node " << i;
    EXPECT_EQ(whole.Nodes()[i].parent, pieces.Nodes()[i].parent) << "node " << i;
    EXPECT_EQ(whole.Nodes()[i].meshes, pieces.Nodes()[i].meshes) << "node " << i;
  }
  ASSERT_EQ(whole.Materials().size(), pieces.Materials().size());
  for (size_t i = 0; i < whole.Materials().size(); i++) {
    EXPECT_EQ(whole.Materials()[i].name, pieces.Materials()[i].name) << "material " << i;
    EXPECT_EQ(whole.Materials()[i].color, pieces.Materials()[i].color) << "material " << i;
    EXPECT_EQ(bool(whole.Materials()[i].texture), bool(pieces.Materials()[i].texture))
        << "material " << i;
  }
  ASSERT_EQ(whole.Meshes().size(), pieces.Meshes().size());
  for (size_t i = 0; i < whole.Meshes().size(); i++) {
    const ModelMesh& a = whole.Meshes()[i];
    const ModelMesh& b = pieces.Meshes()[i];
    if (names) EXPECT_EQ(a.name, b.name) << "mesh " << i;
    EXPECT_EQ(a.node, b.node) << "mesh " << i;
    EXPECT_EQ(MaterialNameOf(whole, a.material), MaterialNameOf(pieces, b.material))
        << "mesh " << i;
    EXPECT_EQ(a.geometry->vertices.size(), b.geometry->vertices.size()) << "mesh " << i;
    EXPECT_EQ(a.geometry->indices.size(), b.geometry->indices.size()) << "mesh " << i;
    if (a.geometry->vertices.size() == b.geometry->vertices.size()) {
      size_t different = 0;
      for (size_t v = 0; v < a.geometry->vertices.size(); v++) {
        const ModelVertex& x = a.geometry->vertices[v];
        const ModelVertex& y = b.geometry->vertices[v];
        if (x.position != y.position || x.normal != y.normal || x.uv != y.uv) different++;
      }
      EXPECT_EQ(different, 0u) << "mesh " << i << " has vertices that differ";
    }
    EXPECT_EQ(a.geometry->indices, b.geometry->indices) << "mesh " << i;
    EXPECT_EQ(a.geometry->bounds_min, b.geometry->bounds_min) << "mesh " << i;
    EXPECT_EQ(a.geometry->bounds_max, b.geometry->bounds_max) << "mesh " << i;
  }
  EXPECT_EQ(whole.BoundsMin(), pieces.BoundsMin());
  EXPECT_EQ(whole.BoundsMax(), pieces.BoundsMax());
}

// A temporary folder with an .obj and its .mtl.
struct ObjFiles {
  explicit ObjFiles(const char* name) : dir(fs::temp_directory_path() / name) {
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "scene.mtl") << "newmtl red\nKd 1 0 0\nnewmtl green\nKd 0 1 0\n"
                                        "newmtl blue\nKd 0 0 1\n";
  }
  ~ObjFiles() { fs::remove_all(dir); }
  std::string Write(const std::string& text) const {
    std::ofstream(dir / "scene.obj") << text;
    return (dir / "scene.obj").string();
  }
  fs::path dir;
};

// `count` objects of a quad and a triangle each, with their own vertices
// ("o"/"g" decides the line each starts with).
std::string ManyObjects(int count, const char* keyword, bool usemtl) {
  std::ostringstream out;
  out << "mtllib scene.mtl\n";
  const char* materials[] = {"red", "green", "blue"};
  int v = 0, vt = 0, vn = 0;  // indices count from the start of the file
  for (int i = 0; i < count; i++) {
    out << keyword << " Object" << i << "\n";
    for (int k = 0; k < 5; k++) {
      out << "v " << i << " " << k << " " << (k * k) % 3 << "\n";
    }
    out << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n";
    if (usemtl) out << "usemtl " << materials[i % 3] << "\n";
    out << "f " << v + 1 << "/" << vt + 1 << "/" << vn + 1 << " " << v + 2 << "/" << vt + 2 << "/"
        << vn + 1 << " " << v + 3 << "/" << vt + 3 << "/" << vn + 1 << " " << v + 4 << "/"
        << vt + 4 << "/" << vn + 1 << "\n";
    out << "f " << v + 1 << "/" << vt + 1 << "/" << vn + 1 << " " << v + 4 << "/" << vt + 4
        << "/" << vn + 1 << " " << v + 5 << "/" << vt + 3 << "/" << vn + 1 << "\n";
    v += 5;
    vt += 4;
    vn += 1;
  }
  return out.str();
}

}  // namespace

TEST(ParallelFor, RunsEveryIndexOnce) {
  std::vector<std::atomic<int>> hits(1000);
  ParallelFor(hits.size(), [&](size_t i) { hits[i]++; });
  for (auto& hit : hits) EXPECT_EQ(hit, 1);
  ParallelFor(0, [](size_t) { FAIL(); });
}

TEST(ParallelFor, RethrowsTheFirstFailure) {
  EXPECT_THROW(ParallelFor(100,
                           [](size_t i) {
                             if (i == 7) throw std::runtime_error("boom");
                           }),
               std::runtime_error);
}

TEST(ParallelObj, PiecesGiveTheSameModelAsTheWholeFile) {
  for (const char* keyword : {"o", "g"}) {
    for (bool usemtl : {true, false}) {
      ObjFiles files("mercury_parallel_obj_test");
      const std::string path = files.Write(ManyObjects(40, keyword, usemtl));
      ASSERT_TRUE(CanSplit(path, 5)) << keyword;
      std::string error;
      auto whole = Model::Load(path, Whole(), &error);
      auto pieces = Model::Load(path, InPieces(5), &error);
      ASSERT_TRUE(whole && pieces) << error;
      EXPECT_EQ(whole->Nodes().size(), 41u);  // the root and 40 objects
      ExpectSameModel(*whole, *pieces);
    }
  }
}

TEST(ParallelObj, ObjectsNamedTwiceJoinIntoOneNode) {
  ObjFiles files("mercury_parallel_obj_dup_test");
  std::string text = ManyObjects(12, "o", true);
  // The last object's name is the first one's again.
  const size_t at = text.rfind("Object11");
  text.replace(at, 8, "Object0");
  const std::string path = files.Write(text);
  ASSERT_TRUE(CanSplit(path, 4));
  auto whole = Model::Load(path, Whole());
  auto pieces = Model::Load(path, InPieces(4));
  ASSERT_TRUE(whole && pieces);
  EXPECT_EQ(whole->Nodes().size(), 12u);
  ExpectSameModel(*whole, *pieces, false);
}

TEST(ParallelObj, FallsBackWhereAFileCannotBeCut) {
  ObjFiles files("mercury_parallel_obj_fallback_test");
  // Relative indices: every face points back from where it is.
  std::ostringstream relative;
  relative << "mtllib scene.mtl\n";
  for (int i = 0; i < 8; i++) {
    relative << "o O" << i << "\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
  }
  EXPECT_FALSE(CanSplit(files.Write(relative.str()), 4));

  // One pool of vertices that every object draws from.
  std::ostringstream pool;
  pool << "mtllib scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n";
  for (int i = 0; i < 8; i++) pool << "o O" << i << "\nf 1 2 3\nf 2 4 3\n";
  EXPECT_FALSE(CanSplit(files.Write(pool.str()), 4));

  // One object cannot be cut at all, and still loads.
  const std::string single = files.Write(ManyObjects(1, "o", true));
  EXPECT_FALSE(CanSplit(single, 4));
  EXPECT_TRUE(Model::Load(single, InPieces(4)));
}

TEST(ParallelObj, GroupsKeepTheirMaterialAcrossACut) {
  ObjFiles files("mercury_parallel_obj_group_test");
  // The material is set once, before the first object; every piece must
  // still use it.
  std::ostringstream out;
  out << "mtllib scene.mtl\nusemtl green\n";
  for (int i = 0; i < 20; i++) {
    out << "o O" << i << "\nv 0 0 " << i << "\nv 1 0 " << i << "\nv 0 1 " << i
        << "\nf " << i * 3 + 1 << " " << i * 3 + 2 << " " << i * 3 + 3 << "\n";
  }
  const std::string path = files.Write(out.str());
  ASSERT_TRUE(CanSplit(path, 5));
  auto whole = Model::Load(path, Whole());
  auto pieces = Model::Load(path, InPieces(5));
  ASSERT_TRUE(whole && pieces);
  ExpectSameModel(*whole, *pieces);
  for (const ModelMesh& mesh : pieces->Meshes()) {
    EXPECT_EQ(pieces->Materials()[mesh.material].name, "green");
  }
}

TEST(ParallelObj, ExampleScenesLoadTheSameInPieces) {
  // Whether each one can be cut: Sponza's faces share one pool of vertices.
  const std::pair<const char*, bool> scenes[] = {
      {"sponza/sponza.obj", false},
      {"conference/conference.obj", true},
      {"breakfast_room/breakfast_room.obj", true},
  };
  for (const auto& [file, splits] : scenes) {
    const std::string path = (kModels / file).string();
    if (!fs::exists(path)) continue;
    std::string error;
    auto whole = Model::Load(path, Whole(), &error);
    auto pieces = Model::Load(path, InPieces(8), &error);
    ASSERT_TRUE(whole && pieces) << file << ": " << error;
    EXPECT_EQ(CanSplit(path, 8), splits) << file;
    ExpectSameModel(*whole, *pieces);
  }
}

TEST(ParallelObj, ReportsProgress) {
  ObjFiles files("mercury_parallel_obj_progress_test");
  const std::string path = files.Write(ManyObjects(30, "o", true));
  std::atomic<float> progress{-1.0f};
  auto model = Model::Load(path, InPieces(3), nullptr, &progress);
  ASSERT_TRUE(model);
  EXPECT_FLOAT_EQ(progress, 1.0f);
}

// Slow (the whole-file import takes a minute): run with
// --gtest_also_run_disabled_tests.
//
// San Miguel has one "o" line that repeats the name of an earlier group. Assimp
// then adds the faces that follow to the mesh before it, where the pieces
// give them to the object of that name, so the meshes are not the same one
// for one: the nodes, materials and everything drawn are.
TEST(ParallelObj, DISABLED_SanMiguelMatchesTheWholeFile) {
  const std::string path = (kModels / "san_miguel/san-miguel-low-poly.obj").string();
  if (!fs::exists(path)) GTEST_SKIP();
  std::string error;
  auto pieces = Model::Load(path, InPieces(0), &error);
  auto whole = Model::Load(path, Whole(), &error);
  ASSERT_TRUE(whole && pieces) << error;

  auto totals = [](const Model& model) {
    std::map<std::string, std::pair<size_t, size_t>> per_material;  // vertices, indices
    for (const ModelMesh& mesh : model.Meshes()) {
      auto& entry = per_material[model.Materials()[mesh.material].name];
      entry.first += mesh.geometry->vertices.size();
      entry.second += mesh.geometry->indices.size();
    }
    return per_material;
  };
  EXPECT_EQ(totals(*whole), totals(*pieces));
  // Each mesh's vertices and triangles, whatever its place in the list.
  auto hashes = [](const Model& model) {
    std::vector<size_t> result;
    for (const ModelMesh& mesh : model.Meshes()) {
      size_t hash = mesh.node * 31u + mesh.material;
      auto mix = [&](float f) { hash = hash * 1000003u ^ std::hash<float>()(f); };
      for (const ModelVertex& v : mesh.geometry->vertices) {
        mix(v.position.x), mix(v.position.y), mix(v.position.z);
        mix(v.normal.x), mix(v.normal.y), mix(v.normal.z);
        mix(v.uv.x), mix(v.uv.y);
      }
      for (uint32_t i : mesh.geometry->indices) hash = hash * 1000003u ^ i;
      result.push_back(hash);
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  {
    const auto a = hashes(*whole), b = hashes(*pieces);
    std::vector<size_t> only_whole, only_pieces;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(only_whole));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(only_pieces));
    // Only the meshes the repeated "o" line moves differ.
    EXPECT_LE(only_whole.size(), 2u);
    EXPECT_LE(only_pieces.size(), 2u);
  }
  ASSERT_EQ(whole->Nodes().size(), pieces->Nodes().size());
  for (size_t i = 0; i < whole->Nodes().size(); i++) {
    EXPECT_EQ(whole->Nodes()[i].name, pieces->Nodes()[i].name) << "node " << i;
  }
  ASSERT_EQ(whole->Materials().size(), pieces->Materials().size());
  for (size_t i = 0; i < whole->Materials().size(); i++) {
    EXPECT_EQ(whole->Materials()[i].name, pieces->Materials()[i].name) << "material " << i;
  }
  EXPECT_EQ(whole->BoundsMin(), pieces->BoundsMin());
  EXPECT_EQ(whole->BoundsMax(), pieces->BoundsMax());
}

TEST(ModelNodes, SameNamedNodesAreFoundByNumber) {
  ObjFiles files("mercury_node_names_test");
  const std::string path = files.Write(
      "mtllib scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
      "g Same\nf 1 2 3\ng Other\nf 1 2 3\ng Same\nf 1 2 3\n");
  auto model = Model::Load(path, Whole());
  ASSERT_TRUE(model);
  const int first = model->FindNode("Same");
  ASSERT_GE(first, 0);
  EXPECT_EQ(model->FindNode("Same#1"), first);
  const int second = model->FindNode("Same#2");
  EXPECT_GT(second, first);
  EXPECT_EQ(model->Nodes()[second].name, "Same");
  EXPECT_NE(model->Nodes()[second].meshes, model->Nodes()[first].meshes);
  EXPECT_EQ(model->FindNode("Same#3"), -1);
  EXPECT_EQ(model->FindNode("Other#1"), model->FindNode("Other"));
  EXPECT_EQ(model->FindNode("Missing#1"), -1);
  EXPECT_EQ(model->FindNode("Same#x"), -1);
}

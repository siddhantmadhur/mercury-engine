/*
 * parallel_obj.h
 *
 * Imports a big .obj file on every core. Assimp reads a file on one thread,
 * which for a model like San Miguel (600 MB of text) is most of the load
 * time. The file is cut at object boundaries ("o" and "g" lines) into pieces
 * that each are a complete .obj of their own: the vertex indices of their
 * faces are rebased, and the active material and material libraries are
 * repeated at the top. The pieces are imported by separate Assimp
 * importers at the same time and joined into one scene, as if the file had
 * been read whole.
 */
#ifndef MERCURY_PROPERTIES_PARALLEL_OBJ_H
#define MERCURY_PROPERTIES_PARALLEL_OBJ_H

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace Mercury {

struct ParallelObjOptions {
  // Files smaller than this are left to a plain import.
  size_t min_bytes = 0;
  // Post-processing flags for every piece (as for Importer::ReadFile).
  unsigned flags = 0;
  // Sets the importer's properties; called once per piece, on its thread.
  std::function<void(Assimp::Importer&)> configure;
  // Called from the worker threads with how much of the import is done (0-1).
  std::function<void(float)> progress;
  // The most pieces to cut the file into; 0 picks from the core count.
  size_t max_pieces = 0;
};

/**
 * @brief Imports `path` in pieces, or returns null if it should be imported
 * whole: the file is small, has one object, uses relative or freeform
 * indices, or a piece failed to import.
 */
std::unique_ptr<aiScene> ImportObjInParallel(const std::string& path,
                                             const ParallelObjOptions& options);

}  // namespace Mercury

#endif  // MERCURY_PROPERTIES_PARALLEL_OBJ_H

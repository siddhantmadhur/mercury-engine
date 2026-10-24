/*
 * model_cache.h
 *
 * Imported models, kept on disk so the next load skips the importer: a
 * binary file per model under os::CacheDir()/models with everything
 * Model::Load builds (nodes, meshes in their final vertex and cluster
 * order, materials, clips) except decoded textures, which are read from
 * their files again (in parallel, as the import does).
 *
 * A cache file is used only while the model file, the .mtl files next to it
 * and the import settings are unchanged (and the format version matches);
 * otherwise the model is imported again and the file rewritten. Models with
 * textures inside the file, and models served from a .mpack, are not cached.
 * MERCURY_MODEL_CACHE=0 turns it off.
 */
#ifndef MERCURY_PROPERTIES_MODEL_CACHE_H
#define MERCURY_PROPERTIES_MODEL_CACHE_H

#include <atomic>
#include <memory>
#include <string>

#include "properties/model.h"

namespace Mercury {

class ModelCache {
 public:
  // The cached model for `path`, or null when there is none that is current.
  static std::shared_ptr<Model> Read(const std::string& path, const ModelImportSettings& settings,
                                     std::atomic<float>* progress);
  // Saves a model Model::Import just built.
  static void Write(const Model& model, const std::string& path,
                    const ModelImportSettings& settings);
};

}  // namespace Mercury

#endif  // MERCURY_PROPERTIES_MODEL_CACHE_H

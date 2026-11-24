// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "input_actions_codegen_util.h"

#include <filesystem>
#include <string>

#include "input/input_actions_codegen.rs.h"
#include "os/filesystem.h"
#include "project/project.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

// Avoid rewriting (and rebuilding every script that imports) a generated
// file when its contents did not change.
bool WriteIfChanged(const fs::path& file, const std::string& text) {
  if (auto current = os::ReadText(file); current && *current == text)
    return true;
  return os::WriteFileAtomic(file, text);
}

}  // namespace

InputActionsHeaderResult WriteInputActionsHeader(
    const Project& project, const InputActionsDesc& desc,
    const std::string& source_path) {
  InputActionsHeaderResult result;
  if (desc.header.empty()) return result;
  const std::string class_name =
      desc.class_name.empty() ? "GameInput" : desc.class_name;
  auto header = GenerateInputActionsAngelScript(desc, class_name, source_path);
  result.errors.reserve(header.errors.size());
  for (const auto& error : header.errors) {
    result.errors.push_back(std::string(error));
  }
  if (!result.errors.empty()) return result;
  if (!WriteIfChanged(project.Resolve(desc.header), std::string(header.source)))
    return result;
  result.written = true;
  return result;
}

}  // namespace Mercury

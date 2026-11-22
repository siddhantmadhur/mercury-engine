// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/json_util.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace Mercury::mcp {

namespace fs = std::filesystem;

bool ParseJson(std::string_view text, nlohmann::json& out) {
  try {
    out = nlohmann::json::parse(text);
    return true;
  } catch (const nlohmann::json::parse_error&) {
    return false;
  }
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& out,
                  std::string* error) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    out = nlohmann::json::object();
    return true;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "could not open " + path.string();
    return false;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  if (!ParseJson(buffer.str(), out)) {
    if (error) *error = path.string() + " is not valid JSON";
    return false;
  }
  return true;
}

bool WriteJsonFile(const fs::path& path, const nlohmann::json& value,
                   std::string* error) {
  // Write next to the target so the rename stays on the same filesystem.
  const fs::path temp =
      path.parent_path() / (path.filename().string() + ".tmp" +
                            std::to_string(std::random_device{}()));
  {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "could not write " + temp.string();
      return false;
    }
    out << value.dump(2) << "\n";
    out.close();
    if (!out) {
      if (error) *error = "could not write " + temp.string();
      fs::remove(temp, ec);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temp, path, ec);
#if defined(_WIN32)
  if (ec) {
    // Windows does not rename over an existing file.
    fs::remove(path, ec);
    fs::rename(temp, path, ec);
  }
#endif
  if (ec) {
    if (error)
      *error = "could not replace " + path.string() + ": " + ec.message();
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

}  // namespace Mercury::mcp

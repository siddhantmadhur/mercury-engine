/*
 * source_map.h
 *
 * Reads the source map (.js.map, version 3) that esbuild writes next to the
 * script bundle, so a position in the bundle can be shown as the TypeScript
 * position it came from: "scripts/ball.ts:42:5".
 */
#ifndef MERCURY_SCRIPTING_JS_SOURCE_MAP_H
#define MERCURY_SCRIPTING_JS_SOURCE_MAP_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mercury {

struct SourcePosition {
  std::string file;
  int line = 0;    // 1-based
  int column = 0;  // 1-based
};

class SourceMap {
 public:
  /**
   * @brief Parses the text of a .js.map file
   * @return false (with `error` set) if it is not a valid version 3 map
   */
  bool Parse(std::string_view json, std::string* error = nullptr);

  /**
   * @brief Maps a bundle position back to its original source
   * @param line 1-based line in the bundle
   * @param column 1-based column in the bundle
   */
  std::optional<SourcePosition> Lookup(int line, int column) const;

  bool IsEmpty() const { return lines_.empty(); }

 private:
  struct Segment {
    int generated_column = 0;  // 0-based
    int source = 0;
    int line = 0;    // 0-based
    int column = 0;  // 0-based
  };
  std::vector<std::string> sources_;
  // One sorted list of segments per generated line.
  std::vector<std::vector<Segment>> lines_;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_JS_SOURCE_MAP_H

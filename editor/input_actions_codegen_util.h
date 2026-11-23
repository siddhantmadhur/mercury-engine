// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * input_actions_codegen_util.h
 *
 * The non-UI half of InputActionsWindow::WriteHeader: generate the
 * AngelScript namespace named by an InputActionsDesc and write it only when
 * generation reports no errors. The window keeps its console messages and its
 * revision bookkeeping; other callers (the MCP authoring tools) use this
 * directly so there is one generator and one WriteIfChanged rule.
 */
#ifndef MERCURY_EDITOR_INPUT_ACTIONS_CODEGEN_UTIL_H
#define MERCURY_EDITOR_INPUT_ACTIONS_CODEGEN_UTIL_H

#include <string>
#include <vector>

#include "input/input_actions.h"

namespace Mercury {

class Project;

struct InputActionsHeaderResult {
  // False when generation failed or the file could not be written.
  bool written = false;
  // Generator diagnostics (duplicate map/action identifiers). A write
  // failure is not a generator diagnostic, so it does not appear here.
  std::vector<std::string> errors;
};

/**
 * @brief Write the generated header for `desc` into the open project
 *
 * `source_path` is the res:// path of the .minput the desc came from; it only
 * appears in the generated file's comment. Nothing is written when the
 * generator reports errors.
 */
InputActionsHeaderResult WriteInputActionsHeader(
    const Project& project, const InputActionsDesc& desc,
    const std::string& source_path);

}  // namespace Mercury

#endif  // MERCURY_EDITOR_INPUT_ACTIONS_CODEGEN_UTIL_H

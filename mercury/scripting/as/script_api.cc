/*
 * script_api.cc
 */
#include "scripting/as/script_api.h"

#include <angelscript.h>

#include "scriptarray/scriptarray.h"
#include "scripting/as/api_prelude.inc"
#include "scripting/as/bindings.h"
#include "scriptmath/scriptmath.h"
#include "scriptstdstring/scriptstdstring.h"

namespace Mercury {

void ConfigureScriptEngine(asIScriptEngine* engine) {
  if (!engine) return;
  RegisterStdString(engine);
  RegisterScriptArray(engine, true);
  RegisterScriptMath(engine);

  engine->RegisterInterface("IBehaviour");
  engine->RegisterInterface("IDataAsset");

  as::InstallAll(engine);
}

const char* ApiPreludeSource() { return kApiPreludeSource; }

}  // namespace Mercury

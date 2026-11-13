/*
 * script_api.h
 *
 * The native AngelScript API setup shared by the game runtime and the editor's
 * script language service. Keeping it in one place means completion and
 * diagnostics see exactly the API scripts are compiled against.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_API_H
#define MERCURY_SCRIPTING_AS_SCRIPT_API_H

class asIScriptEngine;

namespace Mercury {

// Registers the standard add-ons, engine interfaces and the Mercury native
// bindings on `engine`. The engine must already exist.
void ConfigureScriptEngine(asIScriptEngine* engine);

// The AngelScript declarations every script is compiled with. The returned
// string is a process-lifetime constant.
const char* ApiPreludeSource();

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_API_H

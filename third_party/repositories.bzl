"""Repository rules for Mercury's third-party dependencies.

The dependencies stay in their git submodules and keep their own build systems
(CMake). These rules only expose the existing checkout to Bazel as a local,
read-only source tree; rules_foreign_cc then runs the dependency's CMake build.

Also provides small local repositories for host tools (Vulkan, DXC)
and the generated build configuration (absolute checkout/asset paths).
"""

load("@bazel_skylib//lib:paths.bzl", "paths")


# --------------------------------------------------------------------------
# Local dependency checkouts
# --------------------------------------------------------------------------

def _local_source_repo_impl(ctx):
    # Stage the checkout under src/ rather than symlinking it to the repository
    # root: repository_ctx files written at the root would otherwise land inside
    # the (read-only) submodule.
    ctx.symlink(ctx.workspace_root.get_child(ctx.attr.path), "src")
    ctx.file("REPO.bazel", "")
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["src/**"]),
)
""",
    )

local_source_repo = repository_rule(
    implementation = _local_source_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
    },
    local = True,
)


# --------------------------------------------------------------------------
# Vulkan loader + headers
# --------------------------------------------------------------------------

def _find_vulkan(ctx):
    include_dir = None
    lib_dir = None
    version = ""

    sdk = ctx.os.environ.get("VULKAN_SDK")
    if sdk:
        candidate = ctx.path(sdk)
        if candidate.get_child("include/vulkan/vulkan.h").exists:
            include_dir = candidate.get_child("include")
        for lib_name in ["lib", "lib64"]:
            candidate_lib = candidate.get_child(lib_name)
            if candidate_lib.get_child("libvulkan.dylib").exists or \
               candidate_lib.get_child("libvulkan.so").exists or \
               candidate_lib.get_child("vulkan-1.lib").exists:
                lib_dir = candidate_lib
                break

    if include_dir == None:
        pkg = ctx.execute(["pkg-config", "--cflags-only-I", "vulkan"])
        if pkg.return_code == 0:
            for flag in pkg.stdout.split(" "):
                if flag.startswith("-I"):
                    candidate = ctx.path(flag[2:])
                    if candidate.get_child("vulkan/vulkan.h").exists:
                        include_dir = candidate
                        break

    if include_dir == None:
        for candidate in ["/opt/homebrew/include", "/usr/local/include", "/usr/include"]:
            path = ctx.path(candidate)
            if path.get_child("vulkan/vulkan.h").exists:
                include_dir = path
                break

    if include_dir == None:
        fail("Vulkan headers were not found. Install the Vulkan SDK or set VULKAN_SDK.")

    if lib_dir == None:
        pkg = ctx.execute(["pkg-config", "--libs-only-L", "vulkan"])
        if pkg.return_code == 0:
            for flag in pkg.stdout.split(" "):
                if flag.startswith("-L"):
                    candidate = ctx.path(flag[2:])
                    for lib in ["libvulkan.dylib", "libvulkan.so", "vulkan-1.lib"]:
                        if candidate.get_child(lib).exists:
                            lib_dir = candidate
                            break
                    if lib_dir != None:
                        break
    if lib_dir == None:
        for candidate in ["/opt/homebrew/lib", "/usr/local/lib", "/usr/lib", "/usr/lib64"]:
            path = ctx.path(candidate)
            for lib in ["libvulkan.dylib", "libvulkan.so", "vulkan-1.lib"]:
                if path.get_child(lib).exists:
                    lib_dir = path
                    break
            if lib_dir != None:
                break
    if lib_dir == None:
        fail("The Vulkan loader was not found. Install the Vulkan SDK or set VULKAN_SDK.")

    linkopts = []
    is_windows = ctx.os.name.lower().startswith("windows")
    is_macos = ctx.os.name.lower().startswith("mac os")
    if is_windows:
        linkopts.append("/LIBPATH:" + str(lib_dir))
        linkopts.append("vulkan-1.lib")
    else:
        linkopts.append("-L" + str(lib_dir))
        linkopts.append("-lvulkan")
        if is_macos:
            linkopts.append("-Wl,-rpath," + str(lib_dir))

    return include_dir, linkopts


def _vulkan_repo_impl(ctx):
    include_dir, linkopts = _find_vulkan(ctx)
    ctx.symlink(include_dir, "include")
    ctx.file("REPO.bazel", "")
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

cc_library(
    name = "vulkan",
    hdrs = glob([
        "include/vk_video/**",
        "include/vulkan/**",
    ]),
    includes = ["include"],
    linkopts = {linkopts},
)
""".format(linkopts = repr(linkopts)),
    )

vulkan_repo = repository_rule(
    implementation = _vulkan_repo_impl,
    local = True,
)


# --------------------------------------------------------------------------
# wxWidgets (system wx-config)
# --------------------------------------------------------------------------

def _find_wx_config(ctx):
    env = ctx.os.environ
    if env.get("WX_CONFIG"):
        candidate = env["WX_CONFIG"]
        if ctx.path(candidate).exists:
            return candidate
    for name in ["wx-config", "wx-config-gtk3", "wx-config-3.2"]:
        found = ctx.which(name)
        if found:
            return str(found)
    for candidate in ["/usr/bin/wx-config", "/usr/local/bin/wx-config", "/opt/homebrew/bin/wx-config"]:
        if ctx.path(candidate).exists:
            return candidate
    fail("wx-config was not found. Install wxWidgets/wxGTK development packages (e.g. libwxgtk3.2-dev or wxGTK-devel).")


def _wxwidgets_repo_impl(ctx):
    wx_config = _find_wx_config(ctx)

    cflags_res = ctx.execute([wx_config, "--cxxflags"])
    if cflags_res.return_code != 0:
        fail("wx-config --cxxflags failed: " + cflags_res.stderr)

    libs_res = ctx.execute([wx_config, "--libs", "core,base,aui,propgrid,adv,richtext"])
    if libs_res.return_code != 0:
        libs_res = ctx.execute([wx_config, "--libs"])
        if libs_res.return_code != 0:
            fail("wx-config --libs failed: " + libs_res.stderr)

    includes = []
    hdr_globs = []
    defines = []
    copts = []

    inc_idx = 0
    for token in cflags_res.stdout.replace("\n", " ").replace("\t", " ").split(" "):
        if not token:
            continue
        if token.startswith("-I"):
            inc_path = token[2:]
            if inc_path:
                inc_dir = "inc_{}".format(inc_idx)
                inc_idx += 1
                ctx.symlink(inc_path, inc_dir)
                includes.append(inc_dir)
                hdr_globs.append(inc_dir + "/**")
        elif token.startswith("-D"):
            defines.append(token[2:])
        elif not token.startswith("-std="):
            copts.append(token)

    linkopts = []
    for token in libs_res.stdout.replace("\n", " ").replace("\t", " ").split(" "):
        if token:
            linkopts.append(token)

    ctx.file("REPO.bazel", "")
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

cc_library(
    name = "wxwidgets",
    hdrs = glob({hdr_globs}),
    includes = {includes},
    defines = {defines},
    copts = {copts},
    linkopts = {linkopts},
)
""".format(
            hdr_globs = repr(hdr_globs),
            includes = repr(includes),
            defines = repr(defines),
            copts = repr(copts),
            linkopts = repr(linkopts),
        ),
    )

wxwidgets_repo = repository_rule(
    implementation = _wxwidgets_repo_impl,
    local = True,
)


# --------------------------------------------------------------------------
# DXC (HLSL -> SPIR-V)
# --------------------------------------------------------------------------

def _find_dxc(ctx):
    env = ctx.os.environ
    candidates = []
    if env.get("DXC"):
        candidates.append(env["DXC"])
    if env.get("VULKAN_SDK"):
        sdk = env["VULKAN_SDK"]
        candidates.append(paths.join(sdk, "bin/dxc"))
        candidates.append(paths.join(sdk, "bin/dxc.exe"))
    ws = str(ctx.workspace_root)
    candidates += [
        paths.join(ws, "bin/dxc/bin/dxc"),
        paths.join(ws, "bin/vulkansdk/macOS/bin/dxc"),
        paths.join(ws, "bin/vulkansdk/windows/bin/dxc.exe"),
    ]
    for candidate in candidates:
        if ctx.path(candidate).exists:
            return candidate
    result = ctx.execute(["which", "dxc"])
    if result.return_code == 0 and result.stdout.strip():
        return result.stdout.strip()
    fail("dxc was not found. Run scripts/setup_project_*.sh or set VULKAN_SDK.")


def _dxc_repo_impl(ctx):
    dxc = _find_dxc(ctx)
    ctx.file("REPO.bazel", "")
    ctx.file(
        "dxc.sh",
        "#!/bin/sh\nexec {} \"$@\"\n".format(dxc),
        executable = True,
    )
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

sh_binary(
    name = "dxc",
    srcs = ["dxc.sh"],
)
""",
    )

dxc_repo = repository_rule(
    implementation = _dxc_repo_impl,
    local = True,
)


def _yoga_wrapper_repo_impl(ctx):
    ctx.symlink(ctx.workspace_root.get_child("third_party/yoga"), "yoga_src")
    ctx.file("REPO.bazel", "")
    ctx.file(
        "CMakeLists.txt",
        """cmake_minimum_required(VERSION 3.20)
project(mercury_yoga LANGUAGES CXX)

set(YOGA_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/yoga_src)
add_subdirectory(${YOGA_SOURCE}/yoga yoga_build)

install(TARGETS yogacore ARCHIVE DESTINATION lib)
install(
    DIRECTORY ${YOGA_SOURCE}/yoga/
    DESTINATION include/yoga
    FILES_MATCHING PATTERN "*.h"
)
""",
    )
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob([
        "CMakeLists.txt",
        "yoga_src/**",
    ]),
)
""",
    )

yoga_wrapper_repo = repository_rule(
    implementation = _yoga_wrapper_repo_impl,
    local = True,
)


# --------------------------------------------------------------------------
# Generated build configuration
# --------------------------------------------------------------------------

def _config_repo_impl(ctx):
    root = str(ctx.workspace_root)

    values = {
        "MERCURY_ENGINE_DIR": root,
        "MERCURY_SOURCE_DIR": root,
        "MERCURY_SHADER_PATH": paths.join(root, "mercury/shaders"),
        "MERCURY_ASSET_PATH": paths.join(root, "examples/assets"),
        "MERCURY_UI_RESOURCE_PATH": paths.join(root, "mercury/ui/resources"),
        "MERCURY_SPIRV_PATH": paths.join(root, "bazel-bin/mercury/shaders"),
        "MERCURY_GRAPHICS_VULKAN": "1",
        "GLFW_INCLUDE_VULKAN": "1",
        "GLM_FORCE_DEPTH_ZERO_TO_ONE": "1",
    }
    numeric_defines = [
        "MERCURY_GRAPHICS_VULKAN",
        "GLFW_INCLUDE_VULKAN",
        "GLM_FORCE_DEPTH_ZERO_TO_ONE",
    ]
    lines = []
    for name, value in values.items():
        if name in numeric_defines:
            lines.append("-D%s=%s" % (name, value))
        else:
            # copts are shell-tokenized by Bazel; backslash-escape the quotes
            # so the compiler receives a C string literal.
            lines.append('-D%s=\\"%s\\"' % (name, value))
    defs = "COPTS = [\n"
    for line in lines:
        defs += "    %s,\n" % json.encode(line)
    defs += "]\n"
    ctx.file("REPO.bazel", "")
    ctx.file("defs.bzl", defs)
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

exports_files(["defs.bzl"])
""",
    )

build_config_repo = repository_rule(
    implementation = _config_repo_impl,
    local = True,
)

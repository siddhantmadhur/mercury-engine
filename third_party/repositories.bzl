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

# CPython writes __pycache__ next to the modules it imports. Dependency builds
# run Python helper scripts straight from their source tree (Filament's
# spirv-tools grammar generator, for example), and rules_foreign_cc hands that
# tree to the sandbox through symlinks, so the bytecode lands back in the
# source repository. When the repository is a filegroup input of the CMake
# action, that mutates the action's own inputs: Bazel then considers the
# dependency dirty on the next invocation and repeats the whole configure,
# compile and install. Excluding bytecode keeps repository trees stable (and
# leaves the git submodules untouched). The foreign_cc environment additionally
# sets PYTHONDONTWRITEBYTECODE=1 so no new files are created in the first place.
_PYTHON_BYTE_CODE_EXCLUDES = [
    "src/**/__pycache__",
    "src/**/__pycache__/**",
    "src/**/*.pyc",
]

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
    srcs = glob(
        ["src/**"],
        exclude = {excludes},
    ),
)
""".format(excludes = repr(_PYTHON_BYTE_CODE_EXCLUDES)),
    )

local_source_repo = repository_rule(
    implementation = _local_source_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
    },
    local = True,
)


def _exclude_source_repo_impl(ctx):
    # Like local_source_repo, but with a glob exclusion list so build outputs
    # and other large files inside the checkout never become action inputs.
    ctx.symlink(ctx.workspace_root.get_child(ctx.attr.path), "src")
    ctx.file("REPO.bazel", "")
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(
        ["src/**"],
        exclude = {excludes},
    ),
)
""".format(excludes = repr(ctx.attr.excludes + _PYTHON_BYTE_CODE_EXCLUDES)),
    )

exclude_source_repo = repository_rule(
    implementation = _exclude_source_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "excludes": attr.string_list(mandatory = True),
    },
    local = True,
)


# --------------------------------------------------------------------------
# Filament (google/filament)
# --------------------------------------------------------------------------

def _filament_repo_impl(ctx):
    # Filament's checkout carries nested BUILD.bazel files (spirv-headers,
    # meshoptimizer, ...). For Bazel those are package boundaries, so a glob
    # from the root would silently skip them. Copy the tree instead and strip
    # the nested package markers, keeping only CMake-relevant sources.
    ctx.execute(["mkdir", "-p", "src"])
    ctx.report_progress("copying filament sources")
    source = ctx.workspace_root.get_child(ctx.attr.path)
    result = ctx.execute(["cp", "-R", str(source) + "/.", "src/"])
    if result.return_code != 0:
        fail("could not copy filament sources: " + result.stderr)

    for junk in [
        "src/out",
        "src/.git",
        "src/.github",
        "src/web",
        "src/docs",
        "src/docs_src",
        "src/android",
        "src/ios",
        "src/ide",
        "src/skills",
        "src/art",
        "src/assets",
        "src/test",
        # Data only used by the samples and the WebGPU backend, both off.
        "src/third_party/dawn",
        "src/third_party/textures",
        "src/third_party/models",
        "src/third_party/environments",
    ]:
        ctx.execute(["rm", "-rf", junk])

    ctx.report_progress("removing nested package markers")
    # On case-insensitive filesystems Bazel treats a file named "build" as a
    # BUILD package marker, which silently breaks the root glob. Rename those
    # (civetweb ships one); the scripts are not used by CMake builds.
    result = ctx.execute(
        [
            "bash",
            "-c",
            "find src -type d -name __pycache__ -prune -exec rm -rf {} + ; " +
            "find src -type f -name build -exec mv {} {}.script \\; ; " +
            "find src -name BUILD -delete -o -name BUILD.bazel -delete " +
            "-o -name MODULE.bazel -delete -o -name WORKSPACE -delete " +
            "-o -name WORKSPACE.bazel -delete",
        ],
    )
    if result.return_code != 0:
        fail("could not clean filament sources: " + result.stderr)

    ctx.file("REPO.bazel", "")
    ctx.file(
        "BUILD.bazel",
        """package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(
        ["src/**"],
        exclude = {excludes},
    ),
)
""".format(excludes = repr(_PYTHON_BYTE_CODE_EXCLUDES)),
    )

filament_repo = repository_rule(
    implementation = _filament_repo_impl,
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

    # The editor's script view is a wxStyledTextCtrl, so the STC library is
    # required in addition to the panels the editor already used.
    libs_res = ctx.execute([wx_config, "--libs", "core,base,aui,propgrid,adv,richtext,stc"])
    if libs_res.return_code != 0:
        fail("wxWidgets with wxStyledTextCtrl (STC) is required: " +
             "wx-config --libs core,base,aui,propgrid,adv,richtext,stc failed: " +
             libs_res.stderr)

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
# Generated build configuration
# --------------------------------------------------------------------------

def _config_repo_impl(ctx):
    root = str(ctx.workspace_root)

    values = {
        "MERCURY_ENGINE_DIR": root,
        "MERCURY_SOURCE_DIR": root,
        "MERCURY_MATERIAL_PATH": paths.join(root, "bazel-bin/mercury/materials"),
        "MERCURY_ASSET_PATH": paths.join(root, "examples/assets"),
        "MERCURY_UI_RESOURCE_PATH": paths.join(root, "mercury/ui/resources"),
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

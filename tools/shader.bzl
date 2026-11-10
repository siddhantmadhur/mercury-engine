"""Compiles Mercury's HLSL shaders to SPIR-V with DXC."""

def _hlsl_shaders_impl(ctx):
    files_by_name = {file.basename: file for file in ctx.files.srcs}
    outputs = []
    debug = ctx.var.get("COMPILATION_MODE", "fastbuild") == "dbg"

    for job in ctx.attr.jobs:
        src_name, profile, out_name = job.split("|")
        source = files_by_name[src_name]
        out = ctx.actions.declare_file(out_name)
        outputs.append(out)

        args = ctx.actions.args()
        args.add_all([
            "-spirv",
            "-fvk-b-shift",
            "0",
            "0",
            "-fvk-t-shift",
            "10",
            "0",
            "-fvk-s-shift",
            "20",
            "0",
            "-fspv-target-env=vulkan1.3",
            "-T",
            profile,
            "-E",
            "Main",
        ])
        args.add_all(["-Zi", "-Od"] if debug else ["-O3"])
        args.add("-Fo", out)
        args.add(source)

        ctx.actions.run(
            arguments = [args],
            executable = ctx.executable._dxc,
            inputs = ctx.files.srcs + ctx.files.hdrs,
            mnemonic = "DXC",
            outputs = [out],
            progress_message = "DXC %s" % src_name,
        )

    return [DefaultInfo(files = depset(outputs))]


hlsl_shaders = rule(
    implementation = _hlsl_shaders_impl,
    attrs = {
        # "source.hlsl|profile|output.spv"
        "jobs": attr.string_list(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
        "hdrs": attr.label_list(allow_files = True),
        "_dxc": attr.label(
            cfg = "exec",
            default = Label("@dxc//:dxc"),
            executable = True,
        ),
    },
)

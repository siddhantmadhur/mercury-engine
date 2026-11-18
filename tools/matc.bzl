"""Compiles Mercury's Filament material definitions with matc."""

def _filament_material_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.name + ".filamat")
    args = ctx.actions.args()
    args.add("-a", "vulkan")
    args.add("-p", "desktop")
    args.add("-o", out)
    args.add(ctx.file.src)
    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._matc,
        inputs = [ctx.file.src],
        outputs = [out],
        mnemonic = "MATC",
        progress_message = "MATC %s" % ctx.file.src.basename,
    )
    return [DefaultInfo(files = depset([out]))]

filament_material = rule(
    implementation = _filament_material_impl,
    attrs = {
        "src": attr.label(mandatory = True, allow_single_file = [".mat"]),
        "_matc": attr.label(
            # Deliberately the target configuration: filament_build already
            # produces matc as part of the build, and using it here avoids a
            # second full Filament build in the exec configuration just to get
            # the tool. This assumes native builds (the tool must run on the
            # machine doing the build), which is the only supported mode.
            cfg = "target",
            default = Label("//tools:matc"),
            executable = True,
        ),
    },
)

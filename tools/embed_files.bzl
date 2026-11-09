"""A small rule around tools/embed_files.py."""


def _embed_files_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)
    args = ctx.actions.args()
    args.add("--out", out)
    args.add("--root", ctx.attr.root)
    args.add_all([file.path for file in ctx.files.srcs])

    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._tool,
        inputs = ctx.files.srcs,
        mnemonic = "EmbedFiles",
        outputs = [out],
        progress_message = "Embedding %s" % ctx.attr.out,
    )
    return [DefaultInfo(files = depset([out]))]


embed_files = rule(
    implementation = _embed_files_impl,
    attrs = {
        "out": attr.string(mandatory = True),
        "root": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
        "_tool": attr.label(
            cfg = "exec",
            default = Label("//tools:embed_files"),
            executable = True,
        ),
    },
)

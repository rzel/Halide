load("@halide//apps/support:simple_gen.bzl", "simple_gen")
load("@halide//:halide.bzl", "halide_runtime_linkopts")

simple_gen(
    name = "bilateral_grid",
    srcs = ["bilateral_grid.cpp"],
    args = ["8"],
)

cc_binary(
    name = "filter",
    srcs = ["filter.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:runtime",
        "@halide//tools:halide_image_io",
        ":bilateral_grid",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

genrule(
    name = "gen_out_png",
    srcs = ["@halide//apps/images:gray.png"],
    outs = ["out.png"],
    cmd = "$(location :filter) $(location @halide//apps/images:gray.png) $@ 0.1 10",
    tools = [":filter"],
)

# TODO: wire up viz.sh to the relevant rules

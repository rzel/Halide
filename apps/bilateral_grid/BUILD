load("//apps/support:simple_gen.bzl", "simple_gen")

simple_gen(
    name = "bilateral_grid",
    srcs = ["bilateral_grid.cpp"],
    args = ["8"],
)

cc_binary(
    name = "filter",
    srcs = ["filter.cpp"],
    deps = [
        ":bilateral_grid",
        "//:runtime",
        "//apps/support:benchmark",
        "//tools:halide_image_io",
    ],
)

genrule(
    name = "gen_out_png",
    srcs = ["//apps/images:gray.png"],
    outs = ["out.png"],
    cmd = "$(location :filter) $(location //apps/images:gray.png) $@ 0.1 10",
    tools = [":filter"],
)

# TODO: wire up viz.sh to the relevant rules

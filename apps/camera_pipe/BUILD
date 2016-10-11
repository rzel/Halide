load("@halide//apps/support:simple_gen.bzl", "simple_gen")
load("@halide//:halide.bzl", "halide_runtime_linkopts")

simple_gen(
    name = "camera_pipe",
    srcs = ["camera_pipe.cpp"],
    args = ["8"],
)

cc_test(
    name = "process",
    srcs = [
        "fcam/Demosaic.cpp",
        "fcam/Demosaic.h",
        "fcam/Demosaic_ARM.cpp",
        "fcam/Demosaic_ARM.h",
        "process.cpp",
    ],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:runtime",
        "@halide//tools:halide_image_io",
        "@halide//tools:halide_malloc_trace",
        ":camera_pipe",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

TIMING_ITERATIONS = 5

genrule(
    name = "gen_out_png",
    srcs = ["@halide//apps/images:bayer_raw.png"],
    outs = ["out.png"],
    cmd = "$(location :process) $(location @halide//apps/images:bayer_raw.png) 3700 2.0 50 %d $@" % TIMING_ITERATIONS,
    tools = [":process"],
)

# TODO: wire up viz.sh to the relevant rules

load("//apps/support:simple_gen.bzl", "simple_gen")
load("//:halide.bzl", "halide_runtime_linkopts")

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
        ":camera_pipe",
        "//:runtime",
        "//apps/support:benchmark",
        "//tools:halide_image_io",
    ],
)

TIMING_ITERATIONS = 5

genrule(
    name = "gen_out_png",
    srcs = ["//apps/images:bayer_raw.png"],
    outs = ["out.png"],
    cmd = "$(location :process) $(location //apps/images:bayer_raw.png) 3700 2.0 50 %d $@" % TIMING_ITERATIONS,
    tools = [":process"],
)

# TODO: wire up viz.sh to the relevant rules

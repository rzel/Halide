load("@halide//apps/support:simple_gen.bzl", "simple_gen")
load("@halide//:halide.bzl", "halide_runtime_linkopts")

simple_gen(
    name = "halide_blur",
    srcs = ["halide_blur.cpp"],
)

cc_test(
    name = "test",
    srcs = ["test.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":halide_blur",
        "@halide//:runtime",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
    # TODO: detect whether the C++ compiler supports this.
    # copts = ["-fopenmp"]
)

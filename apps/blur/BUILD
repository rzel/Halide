load("//apps/support:simple_gen.bzl", "simple_gen")

simple_gen(
    name = "halide_blur",
    srcs = ["halide_blur.cpp"],
)

cc_test(
    name = "test",
    srcs = ["test.cpp"],
    deps = [
        ":halide_blur",
        "//:runtime",
        "//apps/support:benchmark",
    ],
    # TODO: detect whether the C++ compiler supports this.
    # copts = ["-fopenmp"]
)

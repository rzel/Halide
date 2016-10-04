load("//:halide.bzl", "halide_language_copts")

cc_library(
    name = "benchmark",
    srcs = ["benchmark.h"]
)

[cc_test(
    name = test[0:-4],
    srcs = [test],
    deps = [
        ":benchmark",
        "//:language"
    ],
    copts = halide_language_copts()
) for test in glob(["*.cpp"])]

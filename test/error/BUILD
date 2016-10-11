load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")
load("@halide//test/common:cc_failure_test.bzl", "cc_failure_test")

[cc_failure_test(
    name = "%s" % test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"],
) for test in glob(["*.cpp"])]

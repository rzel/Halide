load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts")
load("//test/common:cc_failure_test.bzl", "cc_failure_test")

[cc_failure_test(
    name = "%s" % test[0:-4],
    srcs = [test],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
) for test in glob(["*.cpp"])]


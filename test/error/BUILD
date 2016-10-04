load("@//:halide.bzl", "halide_language_copts")

[cc_binary(
    name = "error_%s" % test[0:-4],
    srcs = [test],
    deps = ["//:language"],
    copts = halide_language_copts()
) for test in glob(["*.cpp"])]

[sh_test(
    name = test[0:-4],
    srcs = ["expect_failure.sh"],
    args = ["$(location :error_%s)" % test[0:-4]],
    data = [":error_%s" % test[0:-4]],
) for test in glob(["*.cpp"])]

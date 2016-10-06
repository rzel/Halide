load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    deps = ["//:language"] +
           ["//test/common:check_call_graphs"] +
           (["//test/common:gpu_object_lifetime"] if "gpu_object_lifetime" in test else []),
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
) for test in glob(["*.cpp"])]

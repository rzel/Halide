load("//:halide.bzl", "halide_language_copts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    deps = ["//:language"] +
           (["//test/common:gpu_object_lifetime"] if "gpu_object_lifetime" in test else []),
    copts = halide_language_copts()
) for test in glob(["*.cpp"])]

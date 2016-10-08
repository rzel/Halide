load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = ["//:language"] + [
        "//test/common:check_call_graphs",
    ] + (["//test/common:gpu_object_lifetime_tracker"] if ("gpu_object_lifetime" in test) else []),
) for test in glob(["*.cpp"])]

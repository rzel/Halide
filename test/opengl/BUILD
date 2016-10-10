load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_opengl_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts() + halide_opengl_linkopts(),
    deps = [
        "//:language",
        "//:mini_opengl",
    ],
) for test in glob(["*.cpp"])]

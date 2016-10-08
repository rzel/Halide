load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts() +
    # ["-lGL"],
    [
        "-framework OpenGL",  # TODO
    ],
    deps = [
        "//:language",
        "//:mini_opengl",
    ],
) for test in glob(["*.cpp"])]

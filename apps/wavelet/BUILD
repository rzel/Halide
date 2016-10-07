load("@//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_library")

halide_library(
    name = "daubechies_x",
    srcs = [
        "daubechies_constants.h",
        "daubechies_x_generator.cpp",
    ],
    includes=["."],
)

halide_library(
    name = "haar_x",
    srcs = [
        "daubechies_constants.h",
        "haar_x_generator.cpp",
    ],
    includes=["."],
)

halide_library(
    name = "inverse_daubechies_x",
    srcs = [
        "daubechies_constants.h",
        "inverse_daubechies_x_generator.cpp",
    ],
    includes=["."],
)

halide_library(
    name = "inverse_haar_x",
    srcs = [
        "daubechies_constants.h",
        "inverse_haar_x_generator.cpp",
    ],
    includes=["."],
)

cc_binary(
    name = "wavelet",
    srcs = ["wavelet.cpp"],
    deps = [
        ":daubechies_x",
        ":haar_x",
        ":inverse_daubechies_x",
        ":inverse_haar_x",
        "//tools:halide_image_io"
    ]
)

sh_test(
    name = "wavelet_test",
    srcs = ["wavelet_test.sh"],
    args = [
        "$(location :wavelet)",
        "$(location //apps/images:gray.png)",
    ],
    data = [
        ":wavelet",
        "//apps/images:gray.png"
    ],
)


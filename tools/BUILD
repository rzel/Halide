cc_library(
    name = "halide_image_info",
    srcs = ["halide_image_info.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "halide_image_io",
    srcs = ["halide_image_io.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["@png_archive//:png"],
)

cc_binary(
    name = "build_halide_h",
    srcs = ["build_halide_h.cpp"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "bitcode2cpp",
    srcs = ["bitcode2cpp.cpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gengen",
    srcs = [
        "GenGen.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//:language",
    ],
)

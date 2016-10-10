package(
    # TODO
    default_visibility = ["//visibility:public"],
)

load(":llvm_version.bzl", "get_llvm_linkopts")

filegroup(
    name = "llvm-as",
    srcs = ["bin/llvm-as"],
)

filegroup(
    name = "clang",
    srcs = ["bin/clang"],
)

cc_library(
    name = "llvm",
    hdrs = glob([
        "include/llvm/**/*.def",
        "include/llvm/**/*.h",
        "include/llvm-c/**/*.h",
        "build_include/llvm/**/*.h",
        "build_include/llvm/**/*.def",
        "build_include/llvm/**/*.gen",
    ]),
    includes = [
        "build_include",
        "include",
    ],
    # srcs = get_llvm_static_libs(),
    linkopts = get_llvm_linkopts(),
    linkstatic = 1,
)

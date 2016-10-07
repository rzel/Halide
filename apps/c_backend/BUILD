load("@//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

cc_binary(
    name = "pipeline_gen",
    srcs = ["pipeline.cpp"],
    deps = ["@//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

genrule(
    name = "pipeline_genrule",
    tools = [":pipeline_gen"],
    outs = [
        "pipeline_native.h",
        "pipeline_c.h",
        "pipeline_native.o",
        "pipeline_c.cpp",
    ],
    cmd = "$(location :pipeline_gen) $(@D)/"
)

cc_library(
    name = "pipeline",
    srcs = [":pipeline_genrule"],
    includes = ["."]
)

cc_test(
    name = "run",
    srcs = ["run.cpp"],
    deps = [
        ":pipeline",
        "//:runtime",
    ],
)

cc_binary(
    name = "pipeline_cpp_gen",
    srcs = ["pipeline_cpp.cpp"],
    deps = ["@//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

genrule(
    name = "pipeline_cpp_genrule",
    tools = [":pipeline_cpp_gen"],
    outs = [
        "pipeline_cpp_native.h",
        "pipeline_cpp_cpp.h",
        "pipeline_cpp_native.o",
        "pipeline_cpp_cpp.cpp",
    ],
    cmd = "$(location :pipeline_cpp_gen) $(@D)/"
)

cc_library(
    name = "pipeline_cpp",
    srcs = [":pipeline_cpp_genrule"],
    includes = ["."]
)

cc_test(
    name = "run_cpp",
    srcs = ["run_cpp.cpp"],
    deps = [
        ":pipeline_cpp",
        "//:runtime",
    ],
)

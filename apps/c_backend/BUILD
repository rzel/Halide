load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_runtime_linkopts")

cc_binary(
    name = "pipeline_gen",
    srcs = ["pipeline.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"],
)

genrule(
    name = "pipeline_genrule",
    outs = [
        "pipeline_native.h",
        "pipeline_c.h",
        "pipeline_native.o",
        "pipeline_c.cpp",
    ],
    cmd = "$(location :pipeline_gen) $(@D)/",
    tools = [":pipeline_gen"],
)

cc_library(
    name = "pipeline",
    srcs = [":pipeline_genrule"],
    includes = ["."],
)

cc_test(
    name = "run",
    srcs = ["run.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline",
        "@halide//:runtime",
    ],
)

cc_binary(
    name = "pipeline_cpp_gen",
    srcs = ["pipeline_cpp.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"],
)

genrule(
    name = "pipeline_cpp_genrule",
    outs = [
        "pipeline_cpp_native.h",
        "pipeline_cpp_cpp.h",
        "pipeline_cpp_native.o",
        "pipeline_cpp_cpp.cpp",
    ],
    cmd = "$(location :pipeline_cpp_gen) $(@D)/",
    tools = [":pipeline_cpp_gen"],
)

cc_library(
    name = "pipeline_cpp",
    srcs = [":pipeline_cpp_genrule"],
    includes = ["."],
)

cc_test(
    name = "run_cpp",
    srcs = ["run_cpp.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline_cpp",
        "@halide//:runtime",
    ],
)

load("@//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_library")

cc_library(
    name = "extended_buffer_t_common",
    srcs = ["extended_buffer_t_common.h"],
    includes = ["."],
)

# ----------------- Generators

# Generators that don't require any extra args, deps, etc
[halide_library(
    name = gen,
    srcs = ["%s_generator.cpp" % gen],
    includes = ["."],
) for gen in [
    "acquire_release",
    "argvcall",
    "can_use_target",
    "cleanup_on_error",
    "embed_image",
    "error_codes",
    "example",
    "float16_t",
    "gpu_object_lifetime",
    "gpu_only",
    "image_from_array",
    "mandelbrot",
    "matlab",
    "memory_profiler_mandelbrot",
    "metadata_tester",
    "paramtest",
    "tiled_blur_blur",
    "tiled_blur",
    "variable_num_threads",
]]

# Generators that need extra args, etc
halide_library(
    name = "cxx_mangling",
    srcs = ["cxx_mangling_generator.cpp"],
    includes = ["."],
    namespace = "HalideTest",
)

halide_library(
    name = "cxx_mangling_define_extern",
    srcs = ["cxx_mangling_define_extern_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
    namespace = "HalideTest",
    filter_deps = [":cxx_mangling"],  # referenced via define_extern
)

halide_library(
    name = "extended_buffer_t",
    srcs = ["extended_buffer_t_generator.cpp"],
    generator_deps = [":extended_buffer_t_common"],
    includes = ["."],
)

halide_library(
    name = "metadata_tester_ucon",
    srcs = ["metadata_tester_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
)

halide_library(
    name = "msan",
    srcs = ["msan_generator.cpp"],
    halide_target_features = ["msan"],
    includes = ["."],
)

halide_library(
    name = "multitarget",
    srcs = ["multitarget_generator.cpp"],
    halide_target_map = {"*": [
        "*-debug-c_plus_plus_name_mangling",
        "*-c_plus_plus_name_mangling",
    ]},
    includes = ["."],
    namespace = "HalideTest",
)

[halide_library(
    name = "nested_externs_%s" % gen_name,
    srcs = ["nested_externs_generator.cpp"],
    generator_name = "nested_externs_%s" % gen_name,
    includes = ["."],
) for gen_name in [
    "combine",
    "leaf",
]]

halide_library(
    name = "nested_externs_inner",
    srcs = ["nested_externs_generator.cpp"],
    generator_name = "nested_externs_inner",
    includes = ["."],
    filter_deps = [
        ":nested_externs_combine",
        ":nested_externs_leaf",
    ],
)

halide_library(
    name = "nested_externs_root",
    srcs = ["nested_externs_generator.cpp"],
    generator_name = "nested_externs_root",
    includes = ["."],
    filter_deps = [
        ":nested_externs_combine",
        ":nested_externs_inner",
    ],
)

halide_library(
    name = "pyramid",
    srcs = ["pyramid_generator.cpp"],
    generator_args = "levels=10",
    includes = ["."],
)

halide_library(
    name = "tiled_blur_interleaved",
    srcs = ["tiled_blur_generator.cpp"],
    generator_args = "is_interleaved=true",
    includes = ["."],
)

halide_library(
    name = "tiled_blur_blur_interleaved",
    srcs = ["tiled_blur_blur_generator.cpp"],
    generator_args = "is_interleaved=true",
    includes = ["."],
)

halide_library(
    name = "user_context",
    srcs = ["user_context_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
)

halide_library(
    name = "user_context_insanity",
    srcs = ["user_context_insanity_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
)

# ----------------- JIT Tests

# TODO the jit-tests #include .cpp files which confuses Bazel
# (and not unreasonably so); create header-only libraries to work around this
[cc_library(
    name = "%s_jittest_includes" % test,
    hdrs = [
        "%s_generator.cpp" % test,
    ],
) for test in [
    "example",
    "paramtest",
]]

[cc_test(
    name = "%s_jittest" % test,
    srcs = [
        "%s_jittest.cpp" % test,
    ],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":%s_jittest_includes" % test,
        "//:language",
    ],
) for test in [
    "example",
    "paramtest",
]]

# ----------------- AOT Tests

# Simple AOT tests that predictably depend on a single halide_library of the same name
[cc_test(
    name = "%s_aottest" % test,
    srcs = ["%s_aottest.cpp" % test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":%s" % test,
        "//:language",
    ],
) for test in [
    "acquire_release",
    "argvcall",
    "can_use_target",
    "cxx_mangling",
    "cxx_mangling_define_extern",
    "embed_image",
    "error_codes",
    "example",
    "float16_t",
    "gpu_only",
    "image_from_array",
    "mandelbrot",
    "matlab",
    "memory_profiler_mandelbrot",
    "msan",
    "multitarget",
    "pyramid",
    "user_context",
    "user_context_insanity",
    "variable_num_threads",
]]

# AOT tests that have multiple deps, etc
cc_test(
    name = "cleanup_on_error_aottest",
    srcs = ["cleanup_on_error_aottest.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "//:device_interface",
        ":cleanup_on_error",
        "//:language",
    ],
)

cc_test(
    name = "metadata_tester_aottest",
    srcs = ["metadata_tester_aottest.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":metadata_tester",
        ":metadata_tester_ucon",
        "//:language",
    ],
)

cc_test(
    name = "extended_buffer_t_aottest",
    srcs = ["extended_buffer_t_aottest.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":extended_buffer_t",
        ":extended_buffer_t_common",
        "//:language",
    ],
)

cc_test(
    name = "gpu_object_lifetime_aottest",
    srcs = ["gpu_object_lifetime_aottest.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":gpu_object_lifetime",
        "//:language",
        "//test/common:gpu_object_lifetime_tracker",
    ],
)

cc_test(
    name = "nested_externs_aottest",
    srcs = ["nested_externs_aottest.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":nested_externs_root",
        "//:language",
    ],
)

[cc_test(
    name = "tiled_blur%s_aottest" % suffix,
    srcs = ["tiled_blur%s_aottest.cpp" % suffix],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":tiled_blur%s" % suffix,
        ":tiled_blur_blur%s" % suffix,
        "//:language",
    ],
) for suffix in [
    "",
    "_interleaved",
]]

# TODO: NaCl not supported at all
# TODO: should "-c dbg" use the debug runtime?
# TODO: default-copts? default-linkopts?
# TODO: build distrib, build install
# TODO apps/HelloAndroid
# TODO apps/HelloAndroidCamera2
# TODO apps/HelloAndroidGL
# TODO apps/HelloAppleMetal
# TODO apps/HelloHexagon
# TODO apps/HelloMatlab
# TODO apps/HelloiOS
# TODO apps/glsl
# TODO apps/interpolate
# TODO apps/linear_algebra
# TODO apps/local_laplacian
# TODO apps/modules
# TODO apps/nacl_demos
# TODO apps/opengl_demo
# TODO apps/openglcompute
# TODO apps/renderscript
# TODO apps/resize
# TODO apps/simd_op_check
# TODO apps/templates

package(
    default_visibility = ["//visibility:private"],
)

load("@halide//:bazel_helpers/halide_runtime_build_helpers.bzl", "gen_runtime_targets", "runtime_srcs")
load("@llvm//:llvm_version.bzl", "get_llvm_version", "get_llvm_enabled_components")
load("@halide//:halide.bzl", "halide_config_settings")

halide_config_settings()

filegroup(
    name = "runtime_headers",
    srcs = glob(["src/runtime/Halide*.h"]),
)

filegroup(
    name = "language_headers",
    srcs = glob(
        ["src/*.h"],
        exclude = [
            # We must not export files that reference LLVM headers,
            # as clients using prebuilt halide libraries won't
            # have access to them. (No client should need those files,
            # anyway.)
            "src/CodeGen_Internal.h",
            "src/HalideFooter.h",
            "src/LLVM_Headers.h",
        ],
    ),
)

filegroup(
    name = "runtime_files",
    srcs = glob(
        [
            "src/runtime/*",
        ],
    ),
)

genrule(
    name = "build_single_language_header",
    srcs = [
        ":language_headers",
        ":runtime_headers",
        "src/HalideFooter.h",
    ],
    # TODO moving into 'generated' subfolder as workaround for https://github.com/bazelbuild/bazel/issues/1248
    outs = ["generated/Halide.h"],
    # :runtime_headers needs to be made available to the sandbox,
    # but we only want to use the ones referenced indirectly by
    # :language_headers.
    cmd = "$(location @halide//tools:build_halide_h) $(locations :language_headers) $(location src/HalideFooter.h) > $@",
    tools = ["@halide//tools:build_halide_h"],
)

cc_library(
    name = "halide_h",
    srcs = [":build_single_language_header"],
    includes = ["generated"],
    linkstatic = 1,
    visibility = ["//visibility:public"],
)

runtime_cpp_components = [
    "aarch64_cpu_features",
    "android_clock",
    "android_host_cpu_count",
    "android_io",
    "android_opengl_context",
    "android_tempfile",
    "arm_cpu_features",
    "cache",
    "can_use_target",
    "cuda",
    "destructors",
    "device_interface",
    "errors",
    "fake_thread_pool",
    "float16_t",
    "gcd_thread_pool",
    "gpu_device_selection",
    "hexagon_host",
    "ios_io",
    "linux_clock",
    "linux_host_cpu_count",
    "linux_opengl_context",
    "matlab",
    "metadata",
    "metal",
    "metal_objc_arm",
    "metal_objc_x86",
    "mingw_math",
    "mips_cpu_features",
    "module_aot_ref_count",
    "module_jit_ref_count",
    "msan",
    "msan_stubs",
    "nacl_host_cpu_count",
    "noos",
    "opencl",
    "opengl",
    "openglcompute",
    "osx_clock",
    "osx_get_symbol",
    "osx_host_cpu_count",
    "osx_opengl_context",
    "powerpc_cpu_features",
    "posix_allocator",
    "posix_clock",
    "posix_error_handler",
    "posix_get_symbol",
    "posix_io",
    "posix_print",
    "posix_tempfile",
    "posix_threads",
    "profiler",
    "profiler_inlined",
    "qurt_allocator",
    "qurt_hvx",
    "renderscript",
    "runtime_api",
    "ssp",
    "thread_pool",
    "to_string",
    "tracing",
    "windows_clock",
    "windows_cuda",
    "windows_get_symbol",
    "windows_io",
    "windows_opencl",
    "windows_tempfile",
    "windows_threads",
    "write_debug_image",
    "x86_cpu_features",
]

runtime_ll_components = [
    "aarch64",
    "arm",
    "arm_no_neon",
    "hvx_64",
    "hvx_128",
    "mips",
    "pnacl_math",
    "posix_math",
    "powerpc",
    "ptx_dev",
    "renderscript_dev",
    "win32_math",
    "x86",
    "x86_avx",
    "x86_sse41",
]

runtime_nvidia_bitcode_components = [
    "compute_20",
    "compute_30",
    "compute_35",
]

gen_runtime_targets(runtime_cpp_components, runtime_ll_components, runtime_nvidia_bitcode_components)

filegroup(
    name = "runtime_components",
    srcs = runtime_srcs(runtime_cpp_components, runtime_ll_components, runtime_nvidia_bitcode_components),
)

cc_library(
    name = "lib_halide",
    srcs = glob(
        [
            "src/*.cpp",
            "src/BitWriter_3_2/*.cpp",
            "src/*.h",
            "src/BitWriter_3_2/*.h",
            "src/runtime/Halide*.h",
        ],
    ) + [
        ":runtime_headers",
        ":runtime_components",
    ],
    copts = [
        "-DCOMPILING_HALIDE",
        "-DLLVM_VERSION=" + get_llvm_version(),
        "-fno-rtti",
        "-fvisibility-inlines-hidden",
        "-std=c++11",
        "-Wframe-larger-than=131070",  # This applies to the code generator, not the generated code.
    ],
    defines = get_llvm_enabled_components() + [
        "WITH_METAL",
        "WITH_OPENCL",
        "WITH_OPENGL",
        "WITH_RENDERSCRIPT",
    ],
    linkstatic = 1,
    deps = ["@llvm//:llvm"],
)

cc_library(
    name = "language",
    # hdrs = [
    #     ":Halide.h",
    # ],
    visibility = ["//visibility:public"],
    deps = [
        ":halide_h",
        ":lib_halide",
        ":runtime",
    ],
)

# Android needs the -llog flag for __android_log_print
_ANDROID_RUNTIME_LINKOPTS = [
    "-ldl",
    "-llog",
]

_DEFAULT_RUNTIME_LINKOPTS = [
    "-ldl",
]

# This allows runtime files to pull in the definition of buffer_t,
# plus definitions of functions that can be replaced by hosting applications.
cc_library(
    name = "runtime",
    hdrs = [":runtime_headers"],
    includes = ["src/runtime"],
    linkopts = select({
        # There isn't (yet) a good way to make a config that is "Any Android",
        # so we're forced to specialize on all supported Android CPU configs.
        "@halide//:halide_config_arm_32_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_arm_64_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_x86_32_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_x86_64_android": _ANDROID_RUNTIME_LINKOPTS,
        "//conditions:default": _DEFAULT_RUNTIME_LINKOPTS,
    }),
    visibility = ["//visibility:public"],
)

# TODO: should this be moved to a BUILD file in src/runtime?
cc_library(
    name = "mini_opengl",
    testonly = 1,
    hdrs = ["src/runtime/mini_opengl.h"],
    includes = ["src"],
    visibility = ["//test:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

# TODO: should this be moved to a BUILD file in src/runtime?
cc_library(
    name = "device_interface",
    testonly = 1,
    hdrs = ["src/runtime/device_interface.h"],
    includes = ["src"],
    visibility = ["//test:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

cc_library(
    name = "internal_halide_generator_glue",
    srcs = ["@halide//tools:gengen"],
    linkstatic = 1,
    visibility = ["//visibility:public"],
    deps = [
        ":halide_h",
        ":language",
    ],
)

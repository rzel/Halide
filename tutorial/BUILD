load("//:halide.bzl", "halide_language_copts", "halide_language_linkopts")
# load("//apps/support:simple_gen.bzl", "simple_gen")

# TODO: many of these could be collapsed into a list comprehension;
# I've replicated them all explicitly in the theory that for the tutorial
# we're better off being explicit? Not sure
cc_binary(
    name = "lesson_01_basics",
    srcs = ["lesson_01_basics.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_02_input_image",
    srcs = ["lesson_02_input_image.cpp"],
    deps = [
        "//:language",
        "//tools:halide_image_io"
    ],
    data = ["images/rgb.png"],
    copts = halide_language_copts() + 
            ['-DTUTORIAL_IMAGES_DIR=\\"%s/images/\\"' % PACKAGE_NAME],
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_03_debugging_1",
    srcs = ["lesson_03_debugging_1.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_04_debugging_2",
    srcs = ["lesson_04_debugging_2.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_05_scheduling_1",
    srcs = ["lesson_05_scheduling_1.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_06_realizing_over_shifted_domains",
    srcs = ["lesson_06_realizing_over_shifted_domains.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_07_multi_stage_pipelines",
    srcs = ["lesson_07_multi_stage_pipelines.cpp"],
    deps = [
        "//:language",
        "//tools:halide_image_io"
    ],
    data = ["images/rgb.png"],
    copts = halide_language_copts() + 
            ['-DTUTORIAL_IMAGES_DIR=\\"%s/images/\\"' % PACKAGE_NAME],
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_08_scheduling_2",
    srcs = ["lesson_08_scheduling_2.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_09_update_definitions",
    srcs = [
        "lesson_09_update_definitions.cpp",
        "clock.h",
    ],
    deps = [
        "//:language",
        "//tools:halide_image_io"
    ],
    data = ["images/gray.png"],
    copts = halide_language_copts() + 
            ['-DTUTORIAL_IMAGES_DIR=\\"%s/images/\\"' % PACKAGE_NAME],
    linkopts = halide_language_linkopts()
)

# TODO lesson 10

cc_binary(
    name = "lesson_11_cross_compilation",
    srcs = ["lesson_11_cross_compilation.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_12_using_the_gpu",
    srcs = [
        "lesson_12_using_the_gpu.cpp",
        "clock.h",
    ],
    deps = [
        "//:language",
        "//tools:halide_image_io"
    ],
    data = ["images/rgb.png"],
    copts = halide_language_copts() + 
            ['-DTUTORIAL_IMAGES_DIR=\\"%s/images/\\"' % PACKAGE_NAME],
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_13_tuples",
    srcs = ["lesson_13_tuples.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

cc_binary(
    name = "lesson_14_types",
    srcs = ["lesson_14_types.cpp"],
    deps = ["//:language"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts()
)

# TODO lesson 15
# TODO lesson 16




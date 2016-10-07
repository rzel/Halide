load("@//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

def simple_gen(name, srcs, args = []):
    native.cc_binary(
        name = "%s_gen" % name,
        srcs = srcs,
        deps = ["@//:language"],
        copts = halide_language_copts(),
        linkopts = halide_language_linkopts()
    )

    native.genrule(
        name = "%s_lib" % name,
        tools = [":%s_gen" % name],
        outs = ["%s.a" % name, "%s.h" % name],
        cmd = "$(location :%s_gen) " % name + 
              " ".join(args) + 
              " $(@D)/"
    )

    native.cc_library(
        name = name,
        srcs = [":%s_lib" % name],
        hdrs = ["%s.h" % name],
        includes = ["."],
        linkstatic = 1
    )

workspace(name = "halide")

# TODO: specify minimum Bazel version
# check_version("0.3.1")

load("//:bazel_helpers/halide_workspace.bzl", "halide_workspace")
halide_workspace()

# TODO: this is a workaround for https://github.com/bazelbuild/bazel/issues/1248
local_repository(
    name = "halide",
    path = __workspace_dir__,
)

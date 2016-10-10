load(":bazel_helpers/init_llvm_repository.bzl", "init_llvm_repository")

def _check_version(x):
  if native.bazel_version < x:
    fail("Current Bazel version is {}, expected at least {}".format(native.bazel_version, x))

def halide_workspace():
  _check_version("0.3.2")

  # For external dependencies that rarely change, prefer http_archive over git_repository.
  native.new_http_archive(
    name = "zlib_archive",
    url = "http://zlib.net/zlib-1.2.8.tar.gz",
    sha256 = "36658cb768a54c1d4dec43c3116c27ed893e88b02ecfcb44f2166f9c0b7f2a0d",
    strip_prefix = "zlib-1.2.8",
    build_file = "//:bazel_helpers/zlib.BUILD",
  )

  native.new_http_archive(
    name = "png_archive",
    url = "https://github.com/glennrp/libpng/archive/v1.2.53.zip",
    sha256 = "c35bcc6387495ee6e757507a68ba036d38ad05b415c2553b3debe2a57647a692",
    build_file = "//:bazel_helpers/png.BUILD",
    strip_prefix = "libpng-1.2.53",
  )

  init_llvm_repository()

  # repository_ctx.template(
  #     "halide.bzl",
  #     Label("//:bazel_helpers/halide.bzl.tpl"),
  #     {
  #       # "%{llvm_version}": repr(llvm_version[0] + llvm_version[2]),
  #       # "%{llvm_components}": repr(llvm_components),
  #       # "%{llvm_static_libs}": repr(llvm_static_libs),
  #       # "%{llvm_ldflags}": repr(llvm_ldflags),
  #     },
  #     False
  # ) 

  # native.new_git_repository(
  #     name = "llvm",
  #     remote = "http://llvm.org/git/llvm.git",
  #     tag = "branch/release_39",
  #     build_file = "//:bazel_helpers/llvm.BUILD"
  # )

  # native.new_http_archive(
  #     name = "llvm",
  #     url = "http://llvm.org/releases/3.9.0/llvm-3.9.0.src.tar.xz",
  #     sha256 = "66c73179da42cee1386371641241f79ded250e117a79f571bbd69e56daa48948",
  #     strip_prefix = "llvm-3.9.0.src",
  #     build_file = "//:bazel_helpers/llvm.BUILD"
  # )

  # native.new_http_archive(
  #     name = "llvm-clang",
  #     url = "http://llvm.org/releases/3.9.0/cfe-3.9.0.src.tar.xz",
  #     sha256 = "66c73179da42cee1386371641241f79ded250e117a79f571bbd69e56daa48948",
  #     strip_prefix = "cfe-3.9.0.src",
  # )

_components_map = {
  "aarch64" : "WITH_AARCH64",
  "arm" : "WITH_ARM",
  "hexagon" : "WITH_HEXAGON",
  "mips" : "WITH_MIPS",
  "nacltransforms" : "WITH_NATIVE_CLIENT",
  "nvptx" : "WITH_PTX",
  "powerpc" : "WITH_POWERPC",
  "x86" : "WITH_X86",
}

_static_libs_map = %{llvm_static_libs}

def get_llvm_version():
  return %{llvm_version}

def get_llvm_enabled_components():
  flags = []
  for c in %{llvm_components}:
    if c in _components_map:
      flags.append(_components_map[c])
  return flags

# def get_llvm_static_libs():
#     static_libs = []
#     for c in ["bitwriter", "bitreader", "linker", "ipo", "mcjit"] + %{llvm_components}:
#         for lib in _static_libs_map[c]:
#             if not lib in static_libs:
#                 static_libs.append(lib)
#     return [lib.replace("-l", "lib/lib") + ".a" for lib in static_libs]

def get_llvm_linkopts():
  static_libs = []
  for c in ["bitwriter", "bitreader", "linker", "ipo", "mcjit"] + %{llvm_components}:
    for lib in _static_libs_map[c]:
      if not lib in static_libs:
        static_libs.append(lib)
  return %{llvm_ldflags} + %{llvm_system_libs} + static_libs


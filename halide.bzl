# copts requires when depending on the :language target
def halide_language_copts():
    return [
        "-fno-rtti", 
        "-std=c++11", 
        "$(STACK_FRAME_UNLIMITED)",
        "-DGOOGLE_PROTOBUF_NO_RTTI"
    ]

def halide_language_linkopts():
    return [
        "-Wl,-stack_size", "-Wl,1000000"  # TODO OSX ONLY
    ]

# copts requires when depending on the :runtime target
def halide_runtime_copts():
    return []

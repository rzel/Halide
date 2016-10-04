#include "Halide.h"

using namespace Halide;

// Compile a simple pipeline to an object and to C code.
HalideExtern_2(int, an_extern_func, int, int);

int main(int argc, char **argv) {
    Func f, g, h;
    ImageParam input(UInt(16), 2);
    Var x, y;

    f(x, y) = (input(clamp(x+2, 0, input.width()-1), clamp(y-2, 0, input.height()-1)) * 17)/13;

    h.define_extern("an_extern_stage", {f}, Int(16), 0);

    g(x, y) = f(y, x) + f(x, y) + cast<uint16_t>(an_extern_func(x, y)) + h();

    h.compute_root();
    f.compute_root();
    f.debug_to_file("f.tiff");

    std::vector<Argument> args;
    args.push_back(input);

    std::string path(argc > 1 ? argv[1] : "");
    g.compile_to_header(path + "pipeline_native.h", args, "pipeline_native");
    g.compile_to_header(path + "pipeline_c.h", args, "pipeline_c");
    g.compile_to_object(path + "pipeline_native.o", args, "pipeline_native");
    g.compile_to_c(path + "pipeline_c.cpp", args, "pipeline_c");
    return 0;
}

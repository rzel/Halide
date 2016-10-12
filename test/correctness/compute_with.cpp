#include "Halide.h"
#include "../common/check_call_graphs.h"

#include <stdio.h>
#include <map>

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int split_test() {
    Image<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 7);
        g.split(x, xo, xi, 7);
        g.compute_with(f, xo);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int fuse_test() {
    Image<int> im_ref, im;
    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);
        im_ref = h.realize(100, 100, 100);
    }

    {
        Var x("x"), y("y"), z("z"), t("t");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);

        f.compute_root();
        g.compute_root();

        f.fuse(x, y, t).parallel(t);
        g.fuse(x, y, t).parallel(t);
        g.compute_with(f, t);
        im = h.realize(100, 100, 100);
    }

    auto func = [im_ref](int x, int y, int z) {
        return im_ref(x, y, z);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_fuse_group_test() {
    Image<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);
        im_ref = q.realize(200, 200);
    }

    {
        Var x("x"), y("y"), t("t");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();
        p.compute_root();

        p.fuse(x, y, t).parallel(t);
        h.fuse(x, y, t).parallel(t);
        h.compute_with(p, t);

        h.update(1).compute_with(h.update(0), r.x);
        g.update(0).compute_with(g, x);
        f.update(0).compute_with(g, y);
        f.compute_with(g, x);

        im = q.realize(200, 200);
    }

    debug(0) << "COMPARING\n";
    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_outputs_test() {
    const int f_size = 3;
    const int g_size = 4;
    Image<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Image<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 10;
        f(x, y) = print(100 - input(x, y), "\tx: ", x, "\ty: ", y);
        g(x, y) = print(x + input(x, y), "\tx: ", x, "\ty: ", y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 10;
        f(x, y) = print(100 - input(x, y), "\tx: ", x, "\ty: ", y);
        g(x, y) = print(x + input(x, y), "\tx: ", x, "\ty: ", y);

        input.compute_at(f, y);

        //TODO(psuriana): should this be valid???
        //input.compute_at(g, y);

        //input.compute_root();

        f.update(0).compute_with(f, x);
        g.compute_with(f, y);

        Pipeline({f, g}).realize({f_im, g_im});
    }

    std::cout << "Check f\n";
    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    std::cout << "Check g\n";
    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    /*printf("Running split reorder test\n");
    if (split_test() != 0) {
        return -1;
    }

    printf("Running fuse test\n");
    if (fuse_test() != 0) {
        return -1;
    }*/

    /*printf("Running multiple fuse group test\n");
    if (multiple_fuse_group_test() != 0) {
        return -1;
    }*/

    printf("Running multiple outputs test\n");
    if (multiple_outputs_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}

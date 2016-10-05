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

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_outputs_test() {
    Var x("x");
    Func f("f"), g("g"), input("q");

    input(x) = x*x;
    f(x) = 10;
    f(x) += 100*x - input(x - 1);
    g(x) = x + input(x);

    f.update(0).compute_with(f, x);
    g.compute_with(f.update(0), x);
    //g.compute_with(f, x);

    Image<int> f_im(100);
    Image<int> g_im(100);
    Pipeline({f, g}).realize({f_im, g_im});

    for (int x = 0; x < f_im.width(); x++) {
        if (f_im(x) != (100*x - (x - 1)*(x - 1) + 10)) {
            printf("f(%d) = %d instead of %d\n", x, f_im(x), 100*x);
            return -1;
        }
    }

    for (int x = 0; x < g_im.width(); x++) {
        if (g_im(x) != (x + x*x)) {
            printf("g(%d) = %d instead of %d\n", x, f_im(x), x);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    printf("Running split reorder test\n");
    if (split_test() != 0) {
        return -1;
    }

    printf("Running fuse test\n");
    if (fuse_test() != 0) {
        return -1;
    }

    printf("Running multiple fuse group test\n");
    if (multiple_fuse_group_test() != 0) {
        return -1;
    }

    /*printf("Running multiple outputs test\n");
    if (multiple_outputs_test() != 0) {
        return -1;
    }*/

    printf("Success!\n");
    return 0;
}

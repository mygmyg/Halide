#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    //int W = 64*3, H = 64*3;
    const int W = 16, H = 16;

    Image<uint16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }


    Var x("x"), y("y");

    // The kernels in this test are just simple box blurs.
    // Although it would be trivial to combine them, the point of the test
    // is to use multiple kernels at once.
    Func box1, box2;
    box1(x, y) = cast<uint16_t>(1);
    box2(x, y) = cast<uint32_t>(2);

    Func input("input");
    input(x, y) = in(clamp(x, 0, W-1), clamp(y, 0, H-1));
    input.compute_root();

    RDom r(0, 3, 0, 3);

    /* This iterates over r outermost. I.e. the for loop looks like:
     * for y:
     *   for x:
     *     blur1(x, y) = 0
     * for r.y:
     *   for r.x:
     *     for y:
     *       for x:
     *         blur1(x, y) += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1)
     *
     * In general, reductions iterate over the reduction domain outermost.
     */
    Func blur1("blur1");
    blur1(x, y) += box1(r.x, r.y) * input(x + r.x - 1, y + r.y - 1)
                 + cast<uint16_t>(box2(r.x, r.y)) * input(x + r.x - 1, y + r.y - 1);


    /* This uses an inline reduction, and is the more traditional way
     * of scheduling a convolution. "sum" creates an anonymous
     * reduction function that is computed within the for loop over x
     * in blur2. blur2 isn't actually a reduction. The loop nest looks like:
     * for y:
     *   for x:
     *     tmp = 0
     *     for r.y:
     *       for r.x:
     *         tmp += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1)
     *     blur(x, y) = tmp
     */
    Func blur2("blur2");
    blur2(x, y) = sum(box1(r.x, r.y) * input(x + r.x - 1, y + r.y - 1))
                + sum(cast<uint16_t>(box2(r.x, r.y)) * input(x + r.x - 1, y + r.y - 1));

    box1.compute_root();
    box2.compute_root();

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        // Initialization (basically memset) done in a GPU kernel
        blur1.gpu_tile(x, y, 16, 16, GPU_Default);

        // Summation is done as an outermost loop is done on the cpu
        blur1.update().gpu_tile(x, y, 16, 16, GPU_Default);

        // Summation is done as a sequential loop within each gpu thread
        blur2.gpu_tile(x, y, 16, 16, GPU_Default);
    } else {
        // Take this opportunity to test scheduling the pure dimensions in a reduction
        Var xi("xi"), yi("yi");
        blur1.tile(x, y, xi, yi, 6, 6);
        blur1.update().tile(x, y, xi, yi, 4, 4).vectorize(xi).parallel(y);

        blur2.vectorize(x, 4).parallel(y);
    }

    Image<uint16_t> out1 = blur1.realize(W, H, target);
    Image<uint16_t> out2 = blur2.realize(W, H, target);

    for (int y = 2; y < H-2; y++) {
        for (int x = 2; x < W-2; x++) {
            uint16_t correct = (in(x-1, y-1) + in(x, y-1) + in(x+1, y-1) +
                                in(x-1, y)   + in(x, y) +   in(x+1, y) +
                                in(x-1, y+1) + in(x, y+1) + in(x+1, y+1)) * 3;

            if (out1(x, y) != correct) {
                printf("out1(%d, %d) = %d instead of %d\n", x, y, out1(x, y), correct);
                return -1;
            }
            if (out2(x, y) != correct) {
                printf("out2(%d, %d) = %d instead of %d\n", x, y, out2(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;

}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The width and height of the output image */
#define WIDTH  1280
#define HEIGHT  720

/* Fill the framebuffer with pixel data.
 * - `unsigned char* fb`: The frame buffer, correctly sized.
 * - `int w`: Width of the image.
 * - `int h`: Height of the image. */
void ppm_pixels(unsigned char* fb, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = 3 * (y * w + x);

            fb[i + 0] = 255;
            fb[i + 1] = 0;
            fb[i + 2] = 0;
        }
    }
}

/* Print a correctly formatted ppm ASCII image.
 * - `unsigned char* fb`: The frame buffer, correctly sized.
 * - `int w`: Width of the image.
 * - `int h`: Height of the image. */
void ppm_print(unsigned char* fb, int w, int h) {
    printf("P3\n%d %d\n255\n", w, h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = 3 * (y * w + x);

            printf("%d %d %d\n", fb[i + 0], fb[i + 1], fb[i + 2]);
        }
    }
}

int main() {
    // Our framebuffer. This is where the image is emitted to.
    unsigned char* fb = malloc(3 * WIDTH * HEIGHT);

    ppm_pixels(fb, WIDTH, HEIGHT);
    ppm_print(fb, WIDTH, HEIGHT);

    free(fb);

    return 0;
}

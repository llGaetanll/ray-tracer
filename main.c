#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The width and height of the output image */
#define WIDTH  1280
#define HEIGHT  720

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

typedef struct { double x, y, z; } vec3;
typedef vec3 color;
typedef vec3 point3;

vec3 vec3_add(vec3 a, vec3 b) {
    return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

vec3 vec3_sub(vec3 a, vec3 b) {
    return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

vec3 vec3_scale(double c, vec3 v) {
    return (vec3){ c * v.x, c * v.y, c * v.z };
}

double vec3_len_sq(vec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

double vec3_len(vec3 v) {
    return sqrt(vec3_len_sq(v));
}

/* Normalize `v`. */
vec3 vec3_unit(vec3 v) {
    return vec3_scale(1 / vec3_len(v), v);
}

double vec3_dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* See: https://en.wikipedia.org/wiki/Cross_product#Coordinate_notation. */
vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

typedef struct { vec3 loc, dir; } ray;

/* Whether the ray intersects the sphere.
 * - `point3* cen`: Center of the sphere.
 * - `double rad`: Radius of the sphere.
 * - `ray* r`: Ray. */
int hit_sphere(point3* cen, double rad, ray* r) {
    vec3 oc = vec3_sub(*cen, r->loc);

    double a = vec3_dot(r->dir, r->dir);
    double b = -2 * vec3_dot(r->dir, oc);
    double c = vec3_dot(oc, oc) - rad * rad;

    double discriminant = b * b - 4 * a * c;

    return (discriminant >= 0);
}

color ray_color(ray* r) {
    point3 cen = { 0, 0, -1 };
    double rad = 0.5;

    if (hit_sphere(&cen, rad, r))
        return (color){ 1, 1, 1 };

    return (color){ 0, 0, 0 };
}

int main() {
    // Our framebuffer. This is where the image is emitted to.
    unsigned char* fb = malloc(3 * WIDTH * HEIGHT);

    double ar      = (double)WIDTH / (double)HEIGHT; // Aspect Ratio
    double foc_len = 1;                              // Focal Length
    point3 cam_cen = (point3){ 0, 0, 0 };            // Camera Center

    double vp_h = 2;         // Viewport Width
    double vp_w = vp_h * ar; // Viewport Height

    vec3 vp_u = { vp_w,     0, 0 }; // Right-facing vector from the top left of the viewport
    vec3 vp_v = { 0,    -vp_h, 0 }; // Down-facing vector from the top left of the viewport

    vec3 px_du = vec3_scale(1 / (double)WIDTH,  vp_u); // Horizontal distance between pixels
    vec3 px_dv = vec3_scale(1 / (double)HEIGHT, vp_v); // Vertical distance between pixels

    // Upper left corner of the viewport
    vec3 vp_ul = vec3_sub(vec3_sub(vec3_sub(cam_cen, (vec3){ 0, 0, foc_len }), vec3_scale(0.5, vp_u)), vec3_scale(0.5, vp_v));

    // Center of the top left pixel of the viewport 
    vec3 px00_loc = vec3_add(vp_ul, vec3_scale(0.5, vec3_add(px_du, px_dv)));

    // Fill the framebuffer pixel by pixel.
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            point3 px_cen = vec3_add(vec3_add(px00_loc, vec3_scale(x, px_du)), vec3_scale(y, px_dv));
            vec3 ray_dir  = vec3_sub(px_cen, cam_cen);

            ray r = { cam_cen, ray_dir };
            color c = ray_color(&r);
            
            int i = 3 * (y * WIDTH + x);

            fb[i + 0] = (int)(c.x * 255);
            fb[i + 1] = (int)(c.y * 255);
            fb[i + 2] = (int)(c.z * 255);
        }
    }

    ppm_print(fb, WIDTH, HEIGHT);

    free(fb);

    return 0;
}

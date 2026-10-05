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

typedef struct { float x, y, z; } vec3;
typedef vec3 color;
typedef vec3 point3;

vec3 vec3_add(vec3 a, vec3 b) {
    return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

vec3 vec3_sub(vec3 a, vec3 b) {
    return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

vec3 vec3_scale(float c, vec3 v) {
    return (vec3){ c * v.x, c * v.y, c * v.z };
}

float vec3_len_sq(vec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float vec3_len(vec3 v) {
    return sqrtf(vec3_len_sq(v));
}

/* Normalize `v`. */
vec3 vec3_unit(vec3 v) {
    return vec3_scale(1 / vec3_len(v), v);
}

float vec3_dot(vec3 a, vec3 b) {
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

/* The point for a ray at a given time.
 * - `ray* r`: Ray.
 * - `float t`: Time. */
point3 ray_at(ray* r, float t) {
    return vec3_add(r->loc, vec3_scale(t, r->dir));
}

/* Whether the ray intersects the sphere.
 * - `point3* cen`: Center of the sphere.
 * - `float rad`: Radius of the sphere.
 * - `ray* r`: Ray. */
float hit_sphere(point3* cen, float rad, ray* r) {
    vec3 oc = vec3_sub(*cen, r->loc);

    float a = vec3_dot(r->dir, r->dir);
    float b = -2 * vec3_dot(r->dir, oc);
    float c = vec3_dot(oc, oc) - rad * rad;

    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0)
        return -1;

    return (-b - sqrtf(discriminant)) / (2.f * a);
}

color ray_color(ray* r) {
    point3 cen = { 0, 0, -1 };
    float rad = 0.5;

    float t = hit_sphere(&cen, rad, r);

    // No sphere hit.
    if (t <= 0) return (color){ 1, 1, 1 };

    // Vector normal to the sphere at the point of intersection.
    vec3 norm = vec3_unit(vec3_sub(ray_at(r, t), cen));

    return vec3_scale(0.5, (color){ norm.x + 1, norm.y + 1, norm.z + 1 });
}

int main() {
    // Our framebuffer. This is where the image is emitted to.
    unsigned char* fb = malloc(3 * WIDTH * HEIGHT);

    float ar       = (float)WIDTH / HEIGHT;          // Aspect Ratio
    float foc_len  = 1;                              // Focal Length
    point3 cam_cen = (point3){ 0, 0, 0 };            // Camera Center

    float vp_h = 2;         // Viewport Width
    float vp_w = vp_h * ar; // Viewport Height

    vec3 vp_u = { vp_w,     0, 0 }; // Right-facing vector from the top left of the viewport
    vec3 vp_v = { 0,    -vp_h, 0 }; // Down-facing vector from the top left of the viewport

    vec3 px_du = vec3_scale(1 / (float)WIDTH,  vp_u); // Horizontal distance between pixels
    vec3 px_dv = vec3_scale(1 / (float)HEIGHT, vp_v); // Vertical distance between pixels

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

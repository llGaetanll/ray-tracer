#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The width and height of the output image */
#define WIDTH      1280
#define HEIGHT      720

/* Number of ray samples per pixel */
#define PX_SAMPLES    8

/* State to kickstart the prf */
#define INIT_STATE   42

#define PI 3.14159265358979323846f

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

/* xorshift on a state.
 * - `s`: The state.
 *
 * See: https://en.wikipedia.org/wiki/Xorshift */
unsigned int rand_xorshift(unsigned int s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;

    return s;
}

/* Pseudorandom function. Calls xorshift and updates the state. */
unsigned int rand_prf(unsigned int* s) {
    *s = rand_xorshift(*s);

    return *s;
}

/* Random float in [0, 1). */
float rand_float(unsigned int* s) {
    unsigned int x = rand_prf(s);

    // We have 32 bits to work with, mantissa takes 24, so shift over 8.
    unsigned int man = x >> (32 - 24);

    return (float)man / (1 << 24);
}

float rand_float_range(unsigned int* s, float lo, float hi) {
    return (hi - lo) * rand_float(s) + lo;
}

/* Random square in x: [-.5, .5] and y: [-.5, .5]. */
vec3 rand_square(unsigned int* s) {
    return (vec3){ rand_float(s) - .5, rand_float(s) - .5, 0 };
}

/* Random unit vector, sampled analytically. */
vec3 rand_vec3_unit(unsigned int* s) {
    float theta = 2.f * PI * rand_float(s);
    float z = rand_float_range(s, -1.f, 1.f);
    float r = sqrtf(1 - z * z);

    return (vec3){ r * cosf(theta), r * sinf(theta), z };
}

typedef struct { point3 v[3]; } tri;

unsigned int read_uint(const unsigned char** buf) {
    // Note endianness.
    unsigned int i = (unsigned int) (*buf)[0]
                   | (unsigned int) (*buf)[1] << 8
                   | (unsigned int) (*buf)[2] << 16
                   | (unsigned int) (*buf)[3] << 24;

    *buf += 4;

    return i;
}

float read_float(const unsigned char** buf) {
    float f;
    unsigned int bs = read_uint(buf);

    memcpy(&f, &bs, sizeof f);

    return f;
}

point3 read_point3(const unsigned char** buf) {
    float x = read_float(buf);
    float y = read_float(buf);
    float z = read_float(buf);

    return (point3) { x, y, z };
}

tri read_tri(const unsigned char** buf) {
    point3 p1 = read_point3(buf);
    point3 p2 = read_point3(buf);
    point3 p3 = read_point3(buf);

    return (tri) { .v = { p1, p2, p3 } };
}

/* Load a binary STL file into a triangle buffer.
 *
 * In a binary STL file we have:
 * - A header of 80 bytes which we skip.
 * - A number of facets uint32_t over 4 bytes.
 * 
 * Then we have a list of facets which contain: the normal vector, vertices 1,
 * 2, and 3, and an attribute field which takes 2 bytes. All fields in little
 * endian.
 *
 * We're not interested in the normal or the attribute field (which is never
 * used anyway) which is why we advance the cursor in the loop that way.
 *
 * This function allocates, caller should free.
 *
 * See: https://en.wikipedia.org/wiki/STL_(file_format)#Binary */
tri* stl_load(const unsigned char* buf, size_t len, size_t* ntris) {
    // Needs at least header and count
    if (len < 84) return NULL;

    buf += 80; // Skip header

    unsigned int count = read_uint(&buf);

    // Each facet is 50 bytes
    if (len != 84 + 50 * (size_t)count) return NULL;

    tri* tb = malloc(count * sizeof(tri));
    if (tb == NULL) return NULL;

    for (size_t i = 0; i < count; i++) {
        buf += 12;              // Skip normal (3 floats)
        tb[i] = read_tri(&buf);
        buf += 2;               // Skip attribute
    }

    *ntris = count;

    return tb;
}

unsigned char* file_read(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);

    long ssize = ftell(f);
    if (ssize < 0) {
        fclose(f);
        return NULL;
    };
    size_t size = ssize;

    fseek(f, 0, SEEK_SET);

    unsigned char* buf = malloc(size);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t size_r = fread(buf, 1, size, f);
    fclose(f);

    if (size != size_r) {
        free(buf);
        return NULL;
    }

    *len = size;

    return buf;
}

/* Compute Möller–Trumbore to find intersection.
 *
 * See: https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm */
float hit_triangle(tri* tr, ray* r) {
    // These vectors are two edges of the triangle.
    vec3 e1 = vec3_sub(tr->v[1], tr->v[0]);
    vec3 e2 = vec3_sub(tr->v[2], tr->v[0]);

    // NOTE: It's just a result of R3 that
    //
    //   det[a, b, c] = dot(a, cross(b, c)).
    //
    // Proof: expand definitions, as usual.
    vec3    p = vec3_cross(r->dir, e2);
    float det = vec3_dot(e1, p);

    // The ray and the triangle are parallel
    if (fabsf(det) <= 1e-8f) return -1;

    float inv = 1 / det;
    vec3   tv = vec3_sub(r->loc, tr->v[0]);

    // Cramer's rule M x = b with
    //   M = [-D, e1, e2]
    //   x = (t, u, v)
    //   b = tv
    float u = vec3_dot(tv, p) * inv;
    if (u < 0 || u > 1) return -1;

    vec3  q = vec3_cross(tv, e1);
    float v = vec3_dot(r->dir, q) * inv;
    if (v < 0 || u + v > 1) return -1;

    float t = vec3_dot(e2, q) * inv;

    return t > 1e-4f ? t : -1;
}

/* Lambertian */
typedef struct { color albedo; } mat_l;

/* Compute the reflected ray on a Lambertian material.
 * - `unsigned int* s`: Random state.
 * - `mat_l* mat`: Lambertian material reference.
 * - `point3 p`: Point of contact with surface.
 * - `vec3 norm`: Normal vector at contact point. 
 * - `color* att`: Attenuation.
 *
 * When a ray hits a Lambertian, the reflected ray has some randomness to it.
 * This is what gives this material its fuzzy look. The attenuation is just the
 * albedo (the color of the material). */
ray mat_lambertian(unsigned int* s, mat_l* mat, point3 p, vec3 norm, color* att) {
    vec3 v = vec3_add(norm, rand_vec3_unit(s));
    if (vec3_len_sq(v) < 1e-8f) v = norm;
    else v = vec3_unit(v);

    *att = mat->albedo;

    return (ray){ .loc = p, .dir = v };
}

color ray_color(unsigned int* s, mat_l* mats, ray* r, tri* tris, size_t ntris) {
    float t = INFINITY;
    int ti  = -1;

    for (size_t i = 0; i < ntris; i++) {
        float ct = hit_triangle(&tris[i], r);
        if (ct > 0 && ct < t) {
            ti = i;
            t  = ct;
        }
    }

    // No hit.
    if (ti == -1) return (color){ 1, 1, 1 };

    // Intersecting triangle
    tri tr = tris[ti];

    // Vector normal to the sphere at the point of intersection.
    vec3 e1 = vec3_sub(tr.v[1], tr.v[0]);
    vec3 e2 = vec3_sub(tr.v[2], tr.v[0]);
    vec3 norm = vec3_unit(vec3_cross(e1, e2));

    color c;
    mat_lambertian(s, &mats[0], ray_at(r, t), norm, &c);

    return c;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.stl>\n", argv[0]);
        return 1;
    }

    size_t fl;
    unsigned char* buf = file_read(argv[1], &fl);
    if (buf == NULL) {
        fprintf(stderr, "Failed to read file: %s\n", argv[1]);
        return 1;
    }

    size_t ntris = 0;
    tri*    tris = stl_load(buf, fl, &ntris);

    // Our lambertian material table
    mat_l mats[] = { { .albedo = { 0.06f, 0.27f, 0.28f } } };

    // RNG seed
    unsigned int state = INIT_STATE;

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

            color c = { 0, 0, 0 };
            for (int i = 0; i < PX_SAMPLES; i++) {
                point3 offset = rand_square(&state);
                point3 px_cen = vec3_add(
                        vec3_add(
                            px00_loc,
                            vec3_scale(x + offset.x, px_du)),
                            vec3_scale(y + offset.y, px_dv));

                vec3 ray_dir = vec3_sub(px_cen, cam_cen);

                ray r = { cam_cen, ray_dir };

                c = vec3_add(c, ray_color(&state, mats, &r, tris, ntris));
            }

            c = vec3_scale(1.0 / PX_SAMPLES, c);
            
            int i = 3 * (y * WIDTH + x);

            fb[i + 0] = (int)(c.x * 255);
            fb[i + 1] = (int)(c.y * 255);
            fb[i + 2] = (int)(c.z * 255);
        }
    }

    ppm_print(fb, WIDTH, HEIGHT);

    free(fb);
    free(tris);
    free(buf);

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The width and height of the output image */
#define WIDTH      1280
#define HEIGHT      720

/* Number of ray samples per pixel */
#define PX_SAMPLES   64

/* Maximum number of bounces for a given ray */
#define MAX_RAY_BNCE 20

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

/* Component-wise multiplication */
vec3 vec3_mul(vec3 a, vec3 b) {
    return (vec3){ a.x * b.x, a.y * b.y, a.z * b.z };
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

/* Reflect a vector about a normal */
vec3 vec3_reflect(vec3 v, vec3 n) {
    return vec3_sub(v, vec3_scale(2.f, vec3_scale(vec3_dot(v, n), n)));
}

/* Refract a vector about a normal.
 * - `vec3 v`: Vector to refract.
 * - `vec3 n`: Normal vector.
 * - `float k`: eta over eta prime in Snell's law */
vec3 vec3_refract(vec3 v, vec3 n, float k) {
    float     cos_theta = fminf(vec3_dot(vec3_scale(-1.f, v), n), 1.f);
    vec3     r_out_perp = vec3_scale(k, vec3_add(v, vec3_scale(cos_theta, n)));
    vec3 r_out_parallel = vec3_scale(-sqrtf(fabsf(1.f - vec3_len_sq(r_out_perp))), n);

    return vec3_add(r_out_perp, r_out_parallel);
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

/* Info store about the intersection. */
typedef struct { ray* r; float t; vec3 norm; int front; } hit;

/* Lambertian */
typedef struct { color albedo; } mat_l;

/* Compute the reflected ray on a Lambertian material.
 * - `unsigned int* s`: Random state.
 * - `mat_l* mat`: Lambertian material reference.
 * - `ray* r`: Incident ray.
 * - `float t`: Time of contact.
 * - `vec3 norm`: Normal vector of contacted surface. 
 * - `color* att`: Attenuation.
 *
 * When a ray hits a Lambertian, the reflected ray has some randomness to it.
 * This is what gives this material its fuzzy look. The attenuation is just the
 * albedo (the color of the material). */
ray mat_lambertian(unsigned int* s, mat_l* mat, hit* h, color* att) {
    point3 p = ray_at(h->r, h->t);

    vec3 v = vec3_add(h->norm, rand_vec3_unit(s));
    if (vec3_len_sq(v) < 1e-8f) v = h->norm;
    else v = vec3_unit(v);

    *att = mat->albedo;

    return (ray){ .loc = p, .dir = v };
}

/* Metal */
typedef struct { color albedo; float fuzz; } mat_m;

/* Compute the reflected ray on a Metal material.
 * - `unsigned int* s`: Random state.
 * - `mat_m* mat`: Metal material reference.
 * - `ray* r`: Incident ray.
 * - `float t`: Time of contact.
 * - `vec3 norm`: Normal vector of contacted surface. 
 * - `color* att`: Attenuation.
 *
 * When a ray hits a Metal, the ray is reflected with some scaled randomness.
 * The scaling is what controls the amount of fuzz. Zero makes a perfectly shiny
 * metal. The attenuation is just the albedo (the color of the material). */
ray mat_metal(unsigned int* s, mat_m* mat, hit* h, color* att) {
    point3  p = ray_at(h->r, h->t);
    vec3 rand = rand_vec3_unit(s);

    vec3 rr;
    rr = vec3_reflect(h->r->dir, h->norm);
    rr = vec3_add(vec3_unit(rr), vec3_scale(mat->fuzz, rand));

    *att = mat->albedo;

    return (ray){ .loc = p, .dir = rr };
}

/* Dielectric */
typedef struct { float ri; } mat_d;

float reflectance(float cosine, float ri) {
    float r0 = (1.f - ri) / (1.f + ri);
    r0 = r0 * r0;
    return r0 + (1.f - r0) * powf(1.f - cosine, 5.f);
}

/* Compute the reflected ray on a Dielectric material.
 * - `unsigned int* s`: Random state.
 * - `mat_d* mat`: Dielectric material reference.
 * - `ray* r`: Incident ray.
 * - `float t`: Time of contact.
 * - `vec3 norm`: Normal vector of contacted surface. 
 * - `color* att`: Attenuation. */
ray mat_dielectric(unsigned int* s, mat_d* mat, hit* h, color* att) {
    point3 p = ray_at(h->r, h->t);

    float ri = h->front ? 1.f / mat->ri : mat->ri;

    vec3 udir = vec3_unit(h->r->dir);

    float cos_theta = fminf(vec3_dot(vec3_scale(-1.f, udir), h->norm), 1.f);
    float sin_theta = sqrtf(fmaxf(0.f, 1.f - cos_theta * cos_theta));

    vec3 rr;
    if (ri * sin_theta > 1.f || reflectance(cos_theta, ri) > rand_float(s))
        rr = vec3_reflect(udir, h->norm);
    else
        rr = vec3_refract(udir, h->norm, ri);

    // No atttenuation for dielectrics
    *att = (color){ 1, 1, 1 };

    return (ray){ .loc = p, .dir = rr };
}

typedef struct { tri* tris; size_t ntris; mat_l* ml; mat_m* mm; mat_d* md; } scene;

/* Compute the ray's color.
 * - `unsigned int* s`: Random state.
 * - `scene* sc`: Scene information.
 * - `ray r`: Starting ray. */
color ray_color(unsigned int* s, scene* sc, ray r) {
    color c = { 1, 1, 1 }; 

    for (int b = 0; b < MAX_RAY_BNCE; b++) {
        float t = INFINITY;
        int ti  = -1;

        for (size_t i = 0; i < sc->ntris; i++) {
            float ct = hit_triangle(&(sc->tris)[i], &r);
            if (ct > 0 && ct < t) {
                ti = i;
                t  = ct;
            }
        }

        // No hit
        if (ti == -1) {
            // The y component of the ray's current direction
            // is used to compute the sky color
            vec3 d = vec3_unit(r.dir);
            float a = 0.5f * (d.y + 1);

            color sky = vec3_add(
                vec3_scale(1.f - a, (color){1,     1,   1}),
                vec3_scale(a,       (color){.5f, .7f, 1.f})
            );

            return vec3_mul(c, sky);
        }

        // Intersecting triangle
        tri tr = (sc->tris)[ti];

        // Vector normal to the sphere at the point of intersection.
        vec3   e1 = vec3_sub(tr.v[1], tr.v[0]);
        vec3   e2 = vec3_sub(tr.v[2], tr.v[0]);
        vec3 norm = vec3_unit(vec3_cross(e1, e2));

        // Front or back face hit
        int front = vec3_dot(r.dir, norm) < 0;
        if (!front) norm = vec3_scale(-1.f, norm);

        hit h = { .r = &r, .t = t, .norm = norm, .front = front };

        color att;
        // r = mat_lambertian(s, &(sc->ml)[0], &h, &att);
        // r = mat_metal(s, &(sc->mm)[0], &h, &att);
        r = mat_dielectric(s, &(sc->md)[0], &h, &att);

        c = vec3_mul(c, att);
    }

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

    mat_l ml[] = { { .albedo = { 0.06f, 0.27f, 0.28f } } };
    mat_m mm[] = { { .albedo = { 0.35f, 0.84f, 0.62f }, .fuzz = 0.98f } };
    mat_d md[] = { { .ri = 1.516f } };

    // Our scene information
    scene sc = {
        .tris  = tris,
        .ntris = ntris,
        .ml    = ml,    // Lambertian materials
        .mm    = mm,    // Metal materials
        .md    = md,    // Dielectric materials
    };

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

                c = vec3_add(c, ray_color(&state, &sc, r));
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

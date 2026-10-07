#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The width and height of the output image */
#define WIDTH      1280
#define HEIGHT      720

/* Number of ray samples per pixel */
#define PX_SAMPLES   64

/* Bounds on bounces for a given ray */
#define MAX_RAY_BNCE 20
#define MIN_RAY_BNCE  4

/* Minimum number of triangles in a BVH node */
#define BVH_MIN_TRI  16

/* State to kickstart the prf */
#define INIT_STATE   42

#define PI 3.14159265358979323846f

float clamp(float f, float lo, float hi) {
    return fminf(fmaxf(f, lo), hi);
}

/* Per-thread seed. Uses Knuth's multiplicative hashing constant. */
unsigned int state_for(int t) {
    unsigned int s = (unsigned int)t * 2654435761u + INIT_STATE;
    return s ? s : 1u; // 0 is a fixed point of xorshift
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

/* Compute the centroid of the triangle. */
point3 tri_centroid(tri* tri) {
    point3 p = { 0, 0, 0 };

    for (int i = 0; i < 3; i++) p = vec3_add(p, tri->v[i]);

    return vec3_scale(1.f / 3, p);
}

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

typedef struct { float lo, hi; } range;

/* Include a value in the range. */
void rng_add(range* rng, float v) {
    rng->lo = fminf(rng->lo, v);
    rng->hi = fmaxf(rng->hi, v);
}

typedef struct { range x, y, z; } aabb;

aabb aabb_new() {
    return (aabb){
        .x = (range){ .lo = INFINITY, .hi = -INFINITY },
        .y = (range){ .lo = INFINITY, .hi = -INFINITY },
        .z = (range){ .lo = INFINITY, .hi = -INFINITY },
    };
}

range aabb_axis(aabb* bbox, size_t axis) {
    if (axis == 0) return bbox->x;
    if (axis == 1) return bbox->y;
    return bbox->z;
}

/* Returns the widest axis of the aabb. x = 0, y = 1, z = 2. */
size_t aabb_axis_widest(aabb* bbox) {
    float dx = bbox->x.hi - bbox->x.lo;
    float dy = bbox->y.hi - bbox->y.lo;
    float dz = bbox->z.hi - bbox->z.lo;

    size_t axis = 0;
    if (dy > dx) axis = 1;
    if (dz > dy && dz > dx) axis = 2;

    return axis;
}

void aabb_add_point3(aabb* bbox, point3 p) {
    rng_add(&(bbox->x), p.x);
    rng_add(&(bbox->y), p.y);
    rng_add(&(bbox->z), p.z);
}

aabb aabb_from_centroids(tri* tri, size_t ntris) {
    aabb bbox = aabb_new();

    for (size_t i = 0; i < ntris; i++) {
        point3 c = tri_centroid(&tri[i]);
        aabb_add_point3(&bbox, c);
    }

    return bbox;
}

aabb aabb_from_tris(tri* tri, size_t ntris) {
    aabb bbox = aabb_new();

    for (size_t i = 0; i < ntris; i++)
        for (int j = 0; j < 3; j++)
            aabb_add_point3(&bbox, tri[i].v[j]);

    return bbox;
}

/* Whether a ray intersects the aabb in a given range. Uses the slab method. */
int aabb_hit(aabb* bbox, ray* r, range t_rng) {
    for (int axis = 0; axis < 3; axis++) {
        range ax = aabb_axis(bbox, axis);

        float l = (axis == 0) ? r->loc.x : (axis == 1) ? r->loc.y : r->loc.z;
        float d = (axis == 0) ? r->dir.x : (axis == 1) ? r->dir.y : r->dir.z;

        float adinv = 1.f / d;

        float t0 = (ax.lo - l) * adinv;
        float t1 = (ax.hi - l) * adinv;

        if (t0 < t1) {
            if (t0 > t_rng.lo) t_rng.lo = t0;
            if (t1 < t_rng.hi) t_rng.hi = t1;
        } else {
            if (t1 > t_rng.lo) t_rng.lo = t1;
            if (t0 < t_rng.hi) t_rng.hi = t0;
        }

        if (t_rng.hi <= t_rng.lo)
            return 0;
    }

    return 1;
}

/* BVH Tree Node.
 *
 * - `aabb bbox`: Bounding box of all the triangles in the node.
 * - `size_t index`: If this is a leaf node, this is the index in the triangle
 *   buffer. Otherwise, the index to the right child node in the tree.
 * - `size_t count`: 0 for non-leaf nodes. Otherwise, counts the number of
 *   triangles belong to this node. */
typedef struct { aabb bbox; size_t index; size_t count; } node;

/* Partition the triangles from [lo..hi] based on a value v and an axis.
 *
 * We use this function to partition our triangles buffer for BVH. At each step,
 * we partition the triangles by a plane of value `v` with axis 0, 1, or 2 (for
 * x, y, and z respectively). */
size_t partition_in_place(tri* tris, size_t lo, size_t hi, float v, size_t axis) {
    if (lo == hi) return 0;

    while (lo < hi) {
        point3 cl = tri_centroid(&tris[lo]);
        point3 ch = tri_centroid(&tris[hi]);

        float vl = (axis == 0) ? cl.x : (axis == 1) ? cl.y : cl.z;
        float vh = (axis == 0) ? ch.x : (axis == 1) ? ch.y : ch.z;

        if (vl < v) {
            lo++;
            continue;
        }

        if (vh >= v) {
            hi--;
            continue;
        }

        tri  tmp = tris[lo];
        tris[lo] = tris[hi];
        tris[hi] = tmp;

        lo++;
        hi--;
    }

    point3 cl = tri_centroid(&tris[lo]);
    float vl = (axis == 0) ? cl.x : (axis == 1) ? cl.y : cl.z;

    return (vl < v) ? lo + 1 : lo;
}

size_t bvh_routine(tri* tris, size_t tlo, size_t thi, node* nodes, size_t* nnodes) {
    aabb bbox_cen = aabb_from_centroids(tris + tlo, thi - tlo);
    size_t axis   = aabb_axis_widest(&bbox_cen);

    float vl = aabb_axis(&bbox_cen, axis).lo;
    float vh = aabb_axis(&bbox_cen, axis).hi;
    float  v = 0.5f * (vl + vh);

    size_t lo = tlo;
    size_t hi = thi - 1;

    aabb bbox = aabb_from_tris(tris + tlo, thi - tlo);
    int  leaf = thi - tlo < BVH_MIN_TRI;

    size_t ni = *nnodes;
    node n = (node){ .bbox = bbox, .index = leaf ? tlo : 0, .count = leaf ? thi - tlo : 0 };

    nodes[ni] = n;
    (*nnodes)++;

    if (!leaf) {
        size_t i = partition_in_place(tris, lo, hi, v, axis);

        // It's rare but possible that we get a case where all the centroids
        // are so close to each other that the partition is completely
        // ineffective. If this is the case, this function recurses without
        // bounds. To prevent this we check for this bound here and make sure we
        // still divide the space. Note that we only need to check the lower
        // bound because pip operates in [tlo, thi).
        if (i == lo)
            i = lo + (hi - lo) / 2;

        bvh_routine(tris, tlo, i, nodes, nnodes);
        nodes[ni].index = bvh_routine(tris, i, thi, nodes, nnodes);
    }

    return ni;
}

/* Compute Bounding Volume Hierarchies for the given triangle buffer.
 * - `tri* tris`: Triangle buffer.
 * - `size_t ntris`: Triangle buffer length
 * - `size_t* nnodes`: BVH node buffer length. Set by this function.
 * Returns node buffer. */
node* bvh(tri* tris, size_t ntris, size_t* nnodes) {
    *nnodes = 0;

    node* nodes = malloc(2 * ntris * sizeof(node));
    if (nodes == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    bvh_routine(tris, 0, ntris, nodes, nnodes);

    return nodes;
}

typedef struct {
    tri*   tris;
    size_t ntris;
    node*  nodes;
    size_t nnodes;
    mat_l* ml;
    mat_m* mm;
    mat_d* md;
} scene;

/* Compute the ray's color.
 * - `unsigned int* s`: Random state.
 * - `scene* sc`: Scene information.
 * - `ray r`: Starting ray. */
color ray_color(unsigned int* s, scene* sc, ray r) {
    color c = { 1, 1, 1 }; 

    for (int b = 0; b < MAX_RAY_BNCE; b++) {
        // TODO: I believe .lo is basically only used to satisfy aabb_hit
        range t_rng = { .lo = 1e-4f, .hi = INFINITY };

        // Triangle index - the triangle that we hit.
        int ti = -1;

        float p;
        p = fmaxf(c.x, fmaxf(c.y, c.z));
        p = fminf(p, 0.95f);

        float u = rand_float(s);
        if (b > MIN_RAY_BNCE && u > p) return (color){ 0, 0, 0 };

        // Note that `nodes` always has a root. Also note
        // that the stack size is bounded by log_2(ntris / 16).
        size_t stack[64];
        stack[0] = 0;
        int   sp = 0; // Stack pointer

        while (sp > -1) {
            size_t i = stack[sp--];
            node* n = &sc->nodes[i];

            if (aabb_hit(&(n->bbox), &r, t_rng)) {
                if (n->count > 0) {
                    // Leaf: Check through all the triangles normally
                    for (size_t j = 0; j < n->count; j++) {
                        tri* tr  = &(sc->tris)[n->index + j];
                        float ct = hit_triangle(tr, &r);

                        if (ct > 0 && ct < t_rng.hi) {
                            ti = n->index + j;
                            t_rng.hi = ct;
                        }
                    }
                } else {
                    // Non-leaf: Add both children to the stack
                    stack[++sp] = i + 1;     // Left child
                    stack[++sp] = n->index; // Right child
                }
            }
        }

        // No hit
        if (ti == -1) {
            // The y component of the ray's current direction
            // is used to compute the sky color
            vec3  d = vec3_unit(r.dir);
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

        hit h = { .r = &r, .t = t_rng.hi, .norm = norm, .front = front };

        color att;
        // r = mat_lambertian(s, &(sc->ml)[0], &h, &att);
        // r = mat_metal(s, &(sc->mm)[0], &h, &att);
        r = mat_dielectric(s, &(sc->md)[0], &h, &att);

        c = vec3_mul(c, att);

        // Russian roulette terminates paths early with probability p, which
        // darkens the image by a factor of `p`. This restores it
        if (b > MIN_RAY_BNCE)
            c = vec3_scale(1.f / p, c);
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

    size_t nnodes = 0;
    node*   nodes = bvh(tris, ntris, &nnodes);

    mat_l ml[] = { { .albedo = { 0.06f, 0.27f, 0.28f } } };
    mat_m mm[] = { { .albedo = { 0.35f, 0.84f, 0.62f }, .fuzz = 0.98f } };
    mat_d md[] = { { .ri = 1.516f } };

    // Our scene information
    scene sc = {
        .tris   = tris,
        .ntris  = ntris,
        .nodes  = nodes,
        .nnodes = nnodes,
        .ml     = ml,     // Lambertian materials
        .mm     = mm,     // Metal materials
        .md     = md,     // Dielectric materials
    };

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
    #pragma omp parallel for
    for (int y = 0; y < HEIGHT; y++) {
        // RNG seed per thread
        unsigned int state = state_for(y);

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

            fb[i + 0] = (int)(clamp(c.x, 0.f, 1.f) * 255);
            fb[i + 1] = (int)(clamp(c.y, 0.f, 1.f) * 255);
            fb[i + 2] = (int)(clamp(c.z, 0.f, 1.f) * 255);
        }
    }

    ppm_print(fb, WIDTH, HEIGHT);

    free(fb);
    free(nodes);
    free(tris);
    free(buf);

    return 0;
}

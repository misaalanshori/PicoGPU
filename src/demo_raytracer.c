#include "shared_state.h"
#include <math.h>

// Raytracer global allocations
float rt_light_angle = 0.0f;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    Vec3 origin;
    Vec3 dir;
} Ray;

static inline Vec3 vec3_add(Vec3 a, Vec3 b) { return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 vec3_sub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 vec3_mul(Vec3 v, float s) { return (Vec3){v.x * s, v.y * s, v.z * s}; }
static inline float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 vec3_normalize(Vec3 v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len > 1e-5f) return vec3_mul(v, 1.0f / len);
    return v;
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// Fast pow(x, 32) using squaring (only 5 multiplications)
static inline float fast_pow32(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    float x8 = x4 * x4;
    float x16 = x8 * x8;
    return x16 * x16;
}

// Fast pow(x, 64) using squaring (only 6 multiplications)
static inline float fast_pow64(float x) {
    float p32 = fast_pow32(x);
    return p32 * p32;
}

// Algebraic Ray-Sphere Intersection
static inline float intersect_sphere(Ray r, Vec3 center, float radius) {
    Vec3 oc = vec3_sub(r.origin, center);
    float b = vec3_dot(oc, r.dir);
    float c = vec3_dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f) return -1.0f;
    float t1 = -b - sqrtf(discriminant);
    if (t1 > 0.001f) return t1;
    float t2 = -b + sqrtf(discriminant);
    if (t2 > 0.001f) return t2;
    return -1.0f;
}

// Ray-Plane Intersection (for horizontal plane y = constant)
static inline float intersect_plane(Ray r, float plane_y) {
    if (fabsf(r.dir.y) < 1e-5f) return -1.0f;
    float t = (plane_y - r.origin.y) / r.dir.y;
    if (t > 0.001f) return t;
    return -1.0f;
}

// Recursive raytracer shading solver (reflectivity, phong highlights, shadows)
static Vec3 trace_ray(Ray r, int depth) {
    if (depth > 3) { // support refraction depth recursion
        return (Vec3){0.4f, 0.6f, 0.9f};
    }
    
    Vec3 light_pos = (Vec3){3.0f * cosf(rt_light_angle), 4.0f, 3.0f + 3.0f * sinf(rt_light_angle)};
    Vec3 rain_light_pos = (Vec3){0.8f, -0.75f, 2.7f}; 
    
    Vec3 sphere_centers[4] = {
        (Vec3){0.0f, 0.3f * sinf(rt_light_angle * 3.0f), 3.0f}, 
        (Vec3){-1.3f, -0.3f, 2.6f}, 
        (Vec3){1.3f, -0.4f, 3.2f},  
        (Vec3){0.0f, -0.3f, 2.0f}   
    };
    float sphere_radii[4] = {0.7f, 0.5f, 0.45f, 0.5f};
    
    float closest_t = 1e9f;
    int hit_object = -1; 
    
    for (int i = 0; i < 4; i++) {
        float t = intersect_sphere(r, sphere_centers[i], sphere_radii[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            hit_object = i;
        }
    }
    
    float t_plane = intersect_plane(r, -0.8f);
    if (t_plane > 0.0f && t_plane < closest_t) {
        closest_t = t_plane;
        hit_object = 4;
    }
    
    if (hit_object == -1) {
        float sky_t = 0.5f * (r.dir.y + 1.0f);
        return vec3_add(vec3_mul((Vec3){0.4f, 0.6f, 0.9f}, sky_t), vec3_mul((Vec3){0.9f, 0.95f, 1.0f}, 1.0f - sky_t));
    }
    
    Vec3 hit_pos = vec3_add(r.origin, vec3_mul(r.dir, closest_t));
    Vec3 N = (hit_object < 4) ? vec3_normalize(vec3_sub(hit_pos, sphere_centers[hit_object])) : (Vec3){0.0f, 1.0f, 0.0f};
    
    Vec3 L = vec3_normalize(vec3_sub(light_pos, hit_pos));
    Ray shadow_ray;
    shadow_ray.origin = vec3_add(hit_pos, vec3_mul(N, 0.001f));
    shadow_ray.dir = L;
    
    bool in_shadow = false;
    float light_dist = vec3_dot(vec3_sub(light_pos, hit_pos), L);
    for (int i = 0; i < 3; i++) {
        float t = intersect_sphere(shadow_ray, sphere_centers[i], sphere_radii[i]);
        if (t > 0.0f && t < light_dist) { in_shadow = true; break; }
    }
    
    Vec3 L2 = vec3_normalize(vec3_sub(rain_light_pos, hit_pos));
    float dist2 = vec3_dot(vec3_sub(rain_light_pos, hit_pos), L2);
    Ray shadow_ray2;
    shadow_ray2.origin = vec3_add(hit_pos, vec3_mul(N, 0.001f));
    shadow_ray2.dir = L2;
    bool in_shadow2 = false;
    for (int i = 0; i < 3; i++) {
        float t = intersect_sphere(shadow_ray2, sphere_centers[i], sphere_radii[i]);
        if (t > 0.0f && t < dist2) { in_shadow2 = true; break; }
    }
    
    float r_time = rt_light_angle * 0.5f;
    Vec3 rain_light_color = (Vec3){0.5f + 0.5f * sinf(r_time), 0.5f + 0.5f * sinf(r_time + 2.0944f), 0.5f + 0.5f * sinf(r_time + 4.1888f)};
    float attenuation = 1.8f / (1.0f + 0.8f * dist2 * dist2);
    
    if (hit_object == 3) {
        float cos_i = vec3_dot(r.dir, N);
        float eta = (cos_i < 0.0f) ? (1.0f / 1.5f) : 1.5f;
        Vec3 norm_refract = (cos_i < 0.0f) ? N : vec3_mul(N, -1.0f);
        if (cos_i < 0.0f) cos_i = -cos_i;
        
        float k = 1.0f - eta * eta * (1.0f - cos_i * cos_i);
        Vec3 refract_color = (k >= 0.0f) ? trace_ray((Ray){vec3_add(hit_pos, vec3_mul(norm_refract, -0.001f)), vec3_normalize(vec3_add(vec3_mul(r.dir, eta), vec3_mul(norm_refract, eta * cos_i - sqrtf(k))))}, depth + 1) : trace_ray((Ray){vec3_add(hit_pos, vec3_mul(N, 0.001f)), vec3_sub(r.dir, vec3_mul(N, 2.0f * vec3_dot(r.dir, N)))}, depth + 1);
        
        float fresnel = 0.04f + 0.96f * powf(1.0f - cos_i, 5.0f);
        Vec3 reflect_color = trace_ray((Ray){vec3_add(hit_pos, vec3_mul(N, 0.001f)), vec3_sub(r.dir, vec3_mul(N, 2.0f * vec3_dot(r.dir, N)))}, depth + 1);
        
        Vec3 glass_base = vec3_add(vec3_mul(reflect_color, fresnel), vec3_mul(refract_color, 1.0f - fresnel));
        float gs1 = (!in_shadow) ? fast_pow64(fmaxf(0.0f, vec3_dot(N, vec3_normalize(vec3_add(L, vec3_normalize(vec3_mul(r.dir, -1.0f))))))) * 0.8f : 0.0f;
        float gs2 = (!in_shadow2) ? fast_pow64(fmaxf(0.0f, vec3_dot(N, vec3_normalize(vec3_add(L2, vec3_normalize(vec3_mul(r.dir, -1.0f))))))) * 0.8f : 0.0f;
        return vec3_add(glass_base, (Vec3){gs1 + gs2 * rain_light_color.x * attenuation, gs1 + gs2 * rain_light_color.y * attenuation, gs1 + gs2 * rain_light_color.z * attenuation});
    }
    
    Vec3 base_color;
    float specular = 0.0f;
    float reflect_factor = 0.0f;
    
    if (hit_object < 3) {
        if (hit_object == 0) {
            base_color = (Vec3){0.95f, 0.95f, 0.95f};
            reflect_factor = 0.85f;
        } else if (hit_object == 1) {
            base_color = (Vec3){0.9f, 0.1f, 0.1f};
            specular = 0.4f;
            reflect_factor = 0.05f;
        } else {
            base_color = (Vec3){0.1f, 0.2f, 0.9f};
            specular = 0.8f;
            reflect_factor = 0.25f;
        }
    } else {
        int tx = (int)(floorf(hit_pos.x * 1.5f));
        int tz = (int)(floorf(hit_pos.z * 1.5f));
        base_color = ((tx + tz) % 2 == 0) ? (Vec3){0.9f, 0.9f, 0.9f} : (Vec3){0.2f, 0.2f, 0.2f};
        reflect_factor = 0.15f;
    }
    
    float diffuse_factor = fmaxf(0.0f, vec3_dot(N, L));
    float light_intensity = in_shadow ? 0.15f : (0.15f + 0.85f * diffuse_factor);
    float spec_factor = (!in_shadow && specular > 0.0f) ? fast_pow32(fmaxf(0.0f, vec3_dot(N, vec3_normalize(vec3_add(L, vec3_normalize(vec3_mul(r.dir, -1.0f))))))) * specular : 0.0f;
    
    float diffuse2 = (!in_shadow2) ? fmaxf(0.0f, vec3_dot(N, L2)) : 0.0f;
    float spec_factor2 = (!in_shadow2 && specular > 0.0f) ? fast_pow32(fmaxf(0.0f, vec3_dot(N, vec3_normalize(vec3_add(L2, vec3_normalize(vec3_mul(r.dir, -1.0f))))))) * specular : 0.0f;
    
    Vec3 lit_color = vec3_add(vec3_mul(base_color, light_intensity), (Vec3){spec_factor, spec_factor, spec_factor});
    Vec3 lit2_base = vec3_add(vec3_mul(base_color, diffuse2), (Vec3){spec_factor2, spec_factor2, spec_factor2});
    lit_color = vec3_add(lit_color, (Vec3){lit2_base.x * rain_light_color.x * attenuation, lit2_base.y * rain_light_color.y * attenuation, lit2_base.z * rain_light_color.z * attenuation});
    
    if (reflect_factor > 0.0f) {
        Vec3 R_dir = vec3_sub(r.dir, vec3_mul(N, 2.0f * vec3_dot(r.dir, N)));
        Vec3 reflected_color = trace_ray((Ray){vec3_add(hit_pos, vec3_mul(N, 0.001f)), R_dir}, depth + 1);
        lit_color = vec3_add(vec3_mul(lit_color, 1.0f - reflect_factor), vec3_mul(reflected_color, reflect_factor));
    }
    
    return lit_color;
}

// 3D Raytracer main loop renderer (renders entire frame on Core 0)
void update_raytracer_simulation(void) {
    if (!img_water) return;
    
    rt_light_angle += 0.04f; // Orbiting light speed
    
    static float rt_cam_angle = 0.0f;
    rt_cam_angle += 0.015f; // Slow camera orbit
    
    Vec3 scene_center = (Vec3){0.0f, -0.2f, 3.0f};
    
    // Zoom in and out dynamically (distance from 2.8 to 5.2)
    float rt_cam_dist = 4.0f + 1.2f * sinf(rt_cam_angle * 0.6f);
    
    // Orbiting camera coordinates (with height variations)
    Vec3 cam_pos;
    cam_pos.x = scene_center.x + rt_cam_dist * sinf(rt_cam_angle);
    cam_pos.y = scene_center.y + 1.0f + 0.4f * cosf(rt_cam_angle * 0.8f);
    cam_pos.z = scene_center.z - rt_cam_dist * cosf(rt_cam_angle);
    
    // Compute Look-At basis vectors
    Vec3 forward = vec3_sub(scene_center, cam_pos);
    Vec3 w = vec3_normalize(forward);
    
    Vec3 world_up = (Vec3){0.0f, 1.0f, 0.0f};
    Vec3 u = vec3_normalize(vec3_cross(world_up, w));
    Vec3 v = vec3_cross(w, u);
    
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    
    for (int y = 0; y < WATER_H; y++) {
        float screen_y = 0.5f - (float)y / (float)WATER_H;
        screen_y *= ((float)WATER_H / (float)WATER_W);
        
        for (int x = 0; x < WATER_W; x++) {
            float screen_x = (float)x / (float)WATER_W - 0.5f;
            
            Ray r;
            r.origin = cam_pos;
            
            // Fov scaling (screen width multiplier)
            Vec3 dir_unnorm = vec3_add(w, vec3_add(vec3_mul(u, screen_x * 1.1f), vec3_mul(v, screen_y * 1.1f)));
            r.dir = vec3_normalize(dir_unnorm);
            
            Vec3 color = trace_ray(r, 0);
            
            // Convert float colors [0.0, 1.0] to RGB565 format
            int r_int = (int)(color.x * 255.0f);
            int g_int = (int)(color.y * 255.0f);
            int b_int = (int)(color.z * 255.0f);
            
            if (r_int < 0) r_int = 0; else if (r_int > 255) r_int = 255;
            if (g_int < 0) g_int = 0; else if (g_int > 255) g_int = 255;
            if (b_int < 0) b_int = 0; else if (b_int > 255) b_int = 255;
            
            pixels[y * WATER_W + x] = (uint16_t)(((r_int & 0xF8) << 8) | ((g_int & 0xFC) << 3) | (b_int >> 3));
        }
    }
    
    lv_obj_invalidate(img_water);
}

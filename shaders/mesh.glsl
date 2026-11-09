@ctype mat4 lv::Mat4

@vs mesh_vs
layout(binding=0) uniform mesh_params {
    mat4 mvp;
    mat4 model;
};

in vec3 position;
in vec3 normal;
in vec4 color0;
out vec3 world_normal;
out vec4 color;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    // Phase 1 model matrices contain rotations only (no non-uniform scaling).
    world_normal = mat3(model) * normal;
    color = color0;
}
@end

@fs mesh_fs
in vec3 world_normal;
in vec4 color;
out vec4 frag_color;

void main() {
    vec3 light_direction = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = max(dot(normalize(world_normal), light_direction), 0.0);
    frag_color = vec4(color.rgb * (0.25 + 0.75 * diffuse), color.a);
}
@end

@program mesh mesh_vs mesh_fs

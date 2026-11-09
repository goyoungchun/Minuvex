@ctype mat4 lv::Mat4
@ctype vec4 lv::detail::ShaderVec4

@vs mesh_vs
layout(binding=0) uniform mesh_params {
    mat4 mvp;
    mat4 normal_matrix;
};

in vec3 position;
in vec3 normal;
in vec4 color0;
in vec2 texcoord0;
out vec3 world_normal;
out vec4 color;
out vec2 uv;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    world_normal = mat3(normal_matrix) * normal;
    color = color0;
    uv = texcoord0;
}
@end

@fs mesh_fs
layout(binding=1) uniform material_params {
    vec4 base_color;
    vec4 light_direction;
    vec4 light_color;
    vec4 options;
};
layout(binding=0) uniform texture2D base_texture;
layout(binding=0) uniform sampler base_sampler;
in vec3 world_normal;
in vec4 color;
in vec2 uv;
out vec4 frag_color;

void main() {
    vec4 albedo = base_color * color * texture(sampler2D(base_texture, base_sampler), uv);
    if (options.x >= 0.0 && albedo.a < options.x) { discard; }
    vec3 normal = normalize(world_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    float diffuse = max(dot(normal, light_direction.xyz), 0.0);
    vec3 illumination = vec3(light_color.w) + light_color.rgb * diffuse * light_direction.w;
    if (options.y > 0.5) { illumination = vec3(1.0); }
    frag_color = vec4(albedo.rgb * illumination, 1.0);
}
@end

@program mesh mesh_vs mesh_fs

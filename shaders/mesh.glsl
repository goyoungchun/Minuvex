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

@vs skin_vs
layout(binding=0) uniform mesh_params {
    mat4 mvp;
    mat4 normal_matrix;
};
layout(binding=2) uniform skin_params {
    mat4 joint_matrices[128];
    vec4 skin_options;
};
in vec3 position;
in vec3 normal;
in vec4 color0;
in vec2 texcoord0;
in uvec4 joint_indices;
in vec4 joint_weights;
out vec3 world_normal;
out vec4 color;
out vec2 uv;
void main() {
    vec4 p = vec4(position,1.0);
    vec3 n = normal;
    if (skin_options.x > 0.5) {
        mat4 skin = joint_weights.x * joint_matrices[joint_indices.x]
                  + joint_weights.y * joint_matrices[joint_indices.y]
                  + joint_weights.z * joint_matrices[joint_indices.z]
                  + joint_weights.w * joint_matrices[joint_indices.w];
        p = skin * p;
        mat3 linear = mat3(skin);
        vec3 a = cross(linear[1],linear[2]);
        vec3 b = cross(linear[2],linear[0]);
        vec3 c = cross(linear[0],linear[1]);
        float determinant = dot(linear[0],a);
        n = abs(determinant) > 0.00000001 ? (mat3(a,b,c)*normal)/determinant : linear*normal;
        if (dot(n,n) < 0.00000001) { n = normal; }
    }
    gl_Position = mvp * p;
    world_normal = mat3(normal_matrix) * n;
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
@program skin skin_vs mesh_fs

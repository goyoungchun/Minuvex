@ctype mat4 lv::Mat4
@ctype vec4 lv::detail::ShaderVec4

@block vertex_common
layout(binding=0) uniform mesh_params {
    mat4 mvp;
    mat4 normal_matrix;
    mat4 model_matrix;
    mat4 shadow_matrix;
};
in vec3 position;
in vec3 normal;
in vec4 color0;
in vec2 texcoord0;
in vec4 tangent;
out vec3 world_normal;
out vec3 world_position;
out vec4 world_tangent;
out vec4 shadow_position;
out vec4 color;
out vec2 uv;
out float bone_weight;
@end
@block skin_common
layout(binding=2) uniform skin_params {
    mat4 joint_matrices[128];
    vec4 skin_options;
};
in uvec4 joint_indices;
in vec4 joint_weights;
@end
@vs mesh_vs
@include_block vertex_common
void main() {
    vec4 p=vec4(position,1.0);
    gl_Position=mvp*p;
    world_position=(model_matrix*p).xyz;
    world_normal=mat3(normal_matrix)*normal;
    world_tangent=vec4(mat3(model_matrix)*tangent.xyz,tangent.w*sign(determinant(mat3(model_matrix))));
    shadow_position=shadow_matrix*p;color=color0;uv=texcoord0;bone_weight=0.0;
}
@end
@vs skin_vs
@include_block vertex_common
@include_block skin_common
void main() {
    vec4 p=vec4(position,1.0);vec3 n=normal;vec3 t=tangent.xyz;float handed=tangent.w;
    if (skin_options.x > 0.5) {
        mat4 skin=joint_weights.x*joint_matrices[joint_indices.x]+joint_weights.y*joint_matrices[joint_indices.y]+joint_weights.z*joint_matrices[joint_indices.z]+joint_weights.w*joint_matrices[joint_indices.w];
        p=skin*p;mat3 linear=mat3(skin);
        vec3 a=cross(linear[1],linear[2]);vec3 b=cross(linear[2],linear[0]);vec3 c=cross(linear[0],linear[1]);float d=dot(linear[0],a);
        n=abs(d) > 0.00000001 ? (mat3(a,b,c)*normal)/d:linear*normal;
        if (dot(n,n) < 0.00000001) {n=normal;}t=linear*t;handed*=sign(d);
    }
    gl_Position=mvp*p;world_position=(model_matrix*p).xyz;world_normal=mat3(normal_matrix)*n;
    world_tangent=vec4(mat3(model_matrix)*t,handed*sign(determinant(mat3(model_matrix))));
    shadow_position=shadow_matrix*p;color=color0;uv=texcoord0;
    bone_weight=skin_options.y < 0.0 ? max(max(joint_weights.x,joint_weights.y),max(joint_weights.z,joint_weights.w)):dot(joint_weights,vec4(equal(joint_indices,uvec4(uint(skin_options.y)))));
    if (skin_options.x < .5) {bone_weight=0.0;}
}
@end
@vs instance_vs
@include_block vertex_common
in vec4 instance0;
in vec4 instance1;
in vec4 instance2;
in vec4 instance3;
void main() {
    mat4 m=mat4(instance0,instance1,instance2,instance3);mat3 linear=mat3(m);
    vec3 a=cross(linear[1],linear[2]),b=cross(linear[2],linear[0]),c=cross(linear[0],linear[1]);float d=dot(linear[0],a);
    vec4 p=m*vec4(position,1);gl_Position=mvp*p;world_position=p.xyz;
    world_normal=(mat3(a,b,c)*normal)/d;world_tangent=vec4(linear*tangent.xyz,tangent.w*sign(d));
    shadow_position=shadow_matrix*p;color=color0;uv=texcoord0;bone_weight=0.0;
}
@end
@fs mesh_fs
layout(binding=1) uniform material_params {
    vec4 base_color;
    vec4 light_direction;
    vec4 light_color;
    vec4 options;
    vec4 normal_options;
    vec4 shadow_options;
    vec4 debug_options;
    vec4 local_positions[8];
    vec4 local_colors[8];
    vec4 local_directions[8];
    vec4 local_cones[8];
};
layout(binding=0) uniform texture2D base_texture;
layout(binding=0) uniform sampler base_sampler;
layout(binding=1) uniform texture2D normal_texture;
layout(binding=1) uniform sampler normal_sampler;
layout(binding=2) uniform texture2D shadow_texture;
layout(binding=2) uniform sampler shadow_sampler;
in vec3 world_normal;
in vec3 world_position;
in vec4 world_tangent;
in vec4 shadow_position;
in vec4 color;
in vec2 uv;
in float bone_weight;
out vec4 frag_color;
float visibility(vec3 n) {
    if (shadow_options.x < 0.5) {return 1.0;}
    vec3 p=shadow_position.xyz/shadow_position.w;vec2 coord=vec2(p.x*.5+.5,.5-p.y*.5);
    if (p.z < 0.0 || p.z > 1.0 || any(lessThan(coord,vec2(0.0))) || any(greaterThan(coord,vec2(1.0)))) {return 1.0;}
    float bias=shadow_options.z+shadow_options.w*(1.0-max(dot(n,light_direction.xyz),0.0));
    if (shadow_options.x < 1.5) {return p.z-bias <= textureLod(sampler2D(shadow_texture,shadow_sampler),coord,0.0).r ? 1.0:0.0;}
    float sum=0.0;for (int y=-1;y<=1;++y) {for (int x=-1;x<=1;++x) {float depth=textureLod(sampler2D(shadow_texture,shadow_sampler),coord+vec2(float(x),float(y))*shadow_options.y,0.0).r;sum+=p.z-bias <= depth ? 1.0:0.0;}}
    return sum/9.0;
}
void main() {
    vec4 albedo=base_color*color*texture(sampler2D(base_texture,base_sampler),uv);
    vec3 geometric=normalize(world_normal);vec3 n=geometric;
    // Evaluate derivatives before dynamic branches (and support meshes without authored TANGENT).
    vec3 dp1=dFdx(world_position),dp2=dFdy(world_position);vec2 duv1=dFdx(uv),duv2=dFdy(uv);
    if (options.x >= 0.0 && albedo.a < options.x) {discard;}
    if (debug_options.x > .5 && debug_options.x < 1.5) {frag_color=vec4(albedo.rgb,1);return;}
    if (debug_options.x > 1.5 && debug_options.x < 2.5) {frag_color=vec4(1);return;}
    if (debug_options.x > 2.5 && debug_options.x < 3.5) {frag_color=vec4(geometric*.5+.5,1);return;}
    if (debug_options.x > 3.5 && debug_options.x < 4.5) {frag_color=vec4(fract(uv),0,1);return;}
    if (debug_options.x > 4.5 && debug_options.x < 5.5) {float depth=debug_options.y/(debug_options.z-gl_FragCoord.z*(debug_options.z-debug_options.y));frag_color=vec4(vec3(depth),1);return;}
    if (debug_options.x > 6.5 && debug_options.x < 7.5) {float w=clamp(bone_weight,0.0,1.0);frag_color=vec4(w,1.0-abs(w*2.0-1.0),1.0-w,1);return;}
    if (normal_options.y > .5) {
        vec3 t;vec3 b;
        if (abs(world_tangent.w) > .5) {
            t=world_tangent.xyz-geometric*dot(geometric,world_tangent.xyz);
            t=dot(t,t) > 0.00000001 ? normalize(t):normalize(cross(abs(geometric.y)<.99 ? vec3(0,1,0):vec3(1,0,0),geometric));
            b=cross(geometric,t)*world_tangent.w;
        } else {
            vec3 p2=cross(dp2,geometric),p1=cross(geometric,dp1);t=p2*duv1.x+p1*duv2.x;b=p2*duv1.y+p1*duv2.y;
            float scale=inversesqrt(max(max(dot(t,t),dot(b,b)),0.00000001));t*=scale;b*=scale;
        }
        vec3 mapped=texture(sampler2D(normal_texture,normal_sampler),uv).xyz*2.0-1.0;mapped.xy*=normal_options.x;
        vec3 transformed=t*mapped.x+b*mapped.y+geometric*mapped.z;
        if (dot(transformed,transformed) > 0.00000001) {n=normalize(transformed);}
    }
    n*=gl_FrontFacing ? 1.0:-1.0;
    float diffuse=max(dot(n,light_direction.xyz),0.0);
    vec3 illumination=vec3(light_color.w)+light_color.rgb*diffuse*light_direction.w*visibility(geometric);
    if (options.w > .5 && options.z > .5) {for (int i=0;i<8;++i) {
        if (i >= int(options.z)) {break;}
        vec3 delta=local_positions[i].xyz-world_position;float distance=length(delta);
        vec3 direction=delta/max(distance,0.00001);float attenuation=pow(max(1.0-distance/max(local_positions[i].w,0.00001),0.0),2.0)/(1.0+distance*distance);
        if (local_cones[i].y > .5) {float cone=dot(-direction,local_directions[i].xyz);float width=max(local_cones[i].x-local_directions[i].w,0.00001);attenuation*=clamp((cone-local_directions[i].w)/width,0.0,1.0);}
        illumination+=local_colors[i].rgb*local_colors[i].w*max(dot(n,direction),0.0)*attenuation;
    }}
    if (options.y > .5) {illumination=vec3(1.0);}
    frag_color=vec4(clamp(albedo.rgb*illumination,vec3(0),vec3(65000)),1.0);
}
@end
@program mesh mesh_vs mesh_fs
@program skin skin_vs mesh_fs
@program mesh_instances instance_vs mesh_fs

@vs shadow_vs
layout(binding=0) uniform shadow_params {mat4 light_mvp;};
in vec3 position;in vec4 color0;in vec2 texcoord0;
out vec2 shadow_uv;out float shadow_alpha;
void main() {gl_Position=light_mvp*vec4(position,1.0);shadow_uv=texcoord0;shadow_alpha=color0.a;}
@end
@vs shadow_skin_vs
layout(binding=0) uniform shadow_params {mat4 light_mvp;};
@include_block skin_common
in vec3 position;in vec4 color0;in vec2 texcoord0;
out vec2 shadow_uv;out float shadow_alpha;
void main() {
    vec4 p=vec4(position,1.0);if (skin_options.x > .5) {p=(joint_weights.x*joint_matrices[joint_indices.x]+joint_weights.y*joint_matrices[joint_indices.y]+joint_weights.z*joint_matrices[joint_indices.z]+joint_weights.w*joint_matrices[joint_indices.w])*p;}
    gl_Position=light_mvp*p;shadow_uv=texcoord0;shadow_alpha=color0.a;
}
@end
@vs shadow_instance_vs
layout(binding=0) uniform shadow_params {mat4 light_mvp;};
in vec3 position;in vec4 color0;in vec2 texcoord0;
in vec4 instance0;in vec4 instance1;in vec4 instance2;in vec4 instance3;
out vec2 shadow_uv;out float shadow_alpha;
void main() {gl_Position=light_mvp*mat4(instance0,instance1,instance2,instance3)*vec4(position,1);shadow_uv=texcoord0;shadow_alpha=color0.a;}
@end
@fs shadow_fs
layout(binding=1) uniform shadow_material {vec4 shadow_mask;};
layout(binding=0) uniform texture2D mask_texture;
layout(binding=0) uniform sampler mask_sampler;
in vec2 shadow_uv;in float shadow_alpha;
out vec4 shadow_depth;
void main() {if (shadow_mask.x >= 0.0 && texture(sampler2D(mask_texture,mask_sampler),shadow_uv).a*shadow_alpha*shadow_mask.y < shadow_mask.x) {discard;}shadow_depth=vec4(gl_FragCoord.z);}
@end
@program shadow_mesh shadow_vs shadow_fs
@program shadow_skin shadow_skin_vs shadow_fs
@program shadow_instances shadow_instance_vs shadow_fs

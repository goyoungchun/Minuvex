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
@block morph_common
struct morph_delta {vec4 position;vec4 normal;vec4 tangent;};
layout(binding=4) readonly buffer morph_data {morph_delta deltas[];};
layout(binding=3) uniform morph_params {vec4 morph_meta;vec4 morph_weights0;vec4 morph_weights1;};
void apply_morph(inout vec4 p,inout vec3 n,inout vec3 t) {
    vec3 original_n=n,original_t=t;bool morph_active=false;
    for (int i=0;i<8;++i) {if (float(i)<morph_meta.y) {vec4 weights=i<4 ? morph_weights0:morph_weights1;int component=i&3;float w=component==0 ? weights.x:component==1 ? weights.y:component==2 ? weights.z:weights.w;morph_active=morph_active || w!=0.0;int index=i*int(morph_meta.x)+gl_VertexIndex;p.xyz+=deltas[index].position.xyz*w;n+=deltas[index].normal.xyz*w;t+=deltas[index].tangent.xyz*w;}}
    if (morph_active) {n=dot(n,n)>1e-16 ? normalize(n):original_n;t=dot(t,t)>1e-16 ? normalize(t):original_t;}
}
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
@vs morph_mesh_vs
@include_block vertex_common
@include_block morph_common
void main() {
    vec4 p=vec4(position,1.0);vec3 n=normal,t=tangent.xyz;apply_morph(p,n,t);
    gl_Position=mvp*p;
    world_position=(model_matrix*p).xyz;
    world_normal=mat3(normal_matrix)*n;
    world_tangent=vec4(mat3(model_matrix)*t,tangent.w*sign(determinant(mat3(model_matrix))));
    shadow_position=shadow_matrix*p;color=color0;uv=texcoord0;bone_weight=0.0;
}
@end
@vs morph_skin_vs
@include_block vertex_common
@include_block skin_common
@include_block morph_common
void main() {
    vec4 p=vec4(position,1.0);vec3 n=normal;vec3 t=tangent.xyz;float handed=tangent.w;apply_morph(p,n,t);
    if (skin_options.x > 0.5) {
        mat4 skin=joint_weights.x*joint_matrices[joint_indices.x]+joint_weights.y*joint_matrices[joint_indices.y]+joint_weights.z*joint_matrices[joint_indices.z]+joint_weights.w*joint_matrices[joint_indices.w];
        p=skin*p;mat3 linear=mat3(skin);
        vec3 a=cross(linear[1],linear[2]);vec3 b=cross(linear[2],linear[0]);vec3 c=cross(linear[0],linear[1]);float d=dot(linear[0],a);
        n=abs(d) > 0.00000001 ? (mat3(a,b,c)*n)/d:linear*n;
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
    vec4 shadow_geometry; // depth range, inverse world width/height, tan(source angle / 2)
    vec4 pcss_options; // blocker samples, filter samples, max radius in texels
    vec4 debug_options;
    vec4 pbr_options;
    vec4 eye_position;
    vec4 view_direction;
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
layout(binding=3) uniform texture2D metallic_roughness_texture;
layout(binding=3) uniform sampler metallic_roughness_sampler;
in vec3 world_normal;
in vec3 world_position;
in vec4 world_tangent;
in vec4 shadow_position;
in vec4 color;
in vec2 uv;
in float bone_weight;
out vec4 frag_color;
vec3 direct_brdf(vec3 base,float metallic,float roughness,vec3 n,vec3 v,vec3 l) {
    float nv=max(dot(n,v),0.00001),nl=max(dot(n,l),0.0);vec3 hv=v+l;
    if (nl<=0.0 || dot(hv,hv)<0.0000001) {return vec3(0);}
    vec3 h=normalize(hv);float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a=roughness*roughness,a2=a*a,d=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/(3.14159265*d*d);
    float gv=2.0*nv/(nv+sqrt(a2+(1.0-a2)*nv*nv));
    float gl=2.0*nl/(nl+sqrt(a2+(1.0-a2)*nl*nl));
    vec3 f0=mix(vec3(.04),base,metallic);vec3 f=f0+(vec3(1)-f0)*pow(1.0-vh,5.0);
    return ((vec3(1)-f)*(1.0-metallic)*base/3.14159265+f*(distribution*gv*gl/max(4.0*nv*nl,0.00001)))*nl;
}
// Fixed progressive disk: deterministic across frames and shared by search/filter.
const vec2 shadow_disk[64]=vec2[64](
    vec2(0.00000000,0.00000000),
    vec2(-0.52139853,0.47764377),
    vec2(0.04371286,-0.49808552),
    vec2(0.52692351,0.68727841),
    vec2(-0.34814879,-0.06158262),
    vec2(0.66704713,-0.42432078),
    vec2(-0.15897452,0.59137729),
    vec2(-0.43113904,-0.83013199),
    vec2(0.23483032,0.08575966),
    vec2(-0.69325917,0.28616731),
    vec2(0.23693711,-0.50632085),
    vec2(0.26977083,0.86007192),
    vec2(-0.37464744,-0.21711585),
    vec2(0.80981677,-0.17803595),
    vec2(-0.38041236,0.54109744),
    vec2(-0.12442994,-0.96021726),
    vec2(0.13517212,0.11392321),
    vec2(-0.72824657,0.03011528),
    vec2(0.37591356,-0.37408420),
    vec2(-0.04082786,0.88294002),
    vec2(-0.25326253,-0.30349315),
    vec2(0.80285798,0.10802341),
    vec2(-0.52319658,0.36402656),
    vec2(0.20894004,-0.92875942),
    vec2(0.15222993,0.26566153),
    vec2(-0.73409909,-0.23419763),
    vec2(0.53223973,-0.24590825),
    vec2(-0.35461975,0.84734576),
    vec2(-0.15829656,-0.44010476),
    vec2(0.75045586,0.39441857),
    vec2(-0.66203911,0.17451138),
    vec2(0.53232008,-0.82788003),
    vec2(0.02117202,0.12319393),
    vec2(-0.56772304,-0.43967664),
    vec2(0.51362850,-0.04255307),
    vec2(-0.59419443,0.64230677),
    vec2(0.00182935,-0.37499554),
    vec2(0.53777052,0.59281351),
    vec2(-0.62233294,-0.05767768),
    vec2(0.75173649,-0.57054119),
    vec2(-0.05002743,0.27499501),
    vec2(-0.40496417,-0.64352857),
    vec2(0.55245079,0.15140386),
    vec2(-0.80962676,0.41548708),
    vec2(0.15666878,-0.42258714),
    vec2(0.31615988,0.77663887),
    vec2(-0.60829196,-0.28828091),
    vec2(0.93294678,-0.28763746),
    vec2(-0.10947045,0.18679192),
    vec2(-0.15526188,-0.72302749),
    vec2(0.44419744,0.31553706),
    vec2(-0.88582567,0.11039875),
    vec2(0.26871687,-0.31569961),
    vec2(0.02987177,0.81913532),
    vec2(-0.45590631,-0.46262775),
    vec2(0.95889060,0.04902872),
    vec2(-0.25495125,0.21065342),
    vec2(0.10786682,-0.77313631),
    vec2(0.33997690,0.49375166),
    vec2(-0.90341756,-0.20787427),
    vec2(0.42121695,-0.23864468),
    vec2(-0.26443876,0.81513627),
    vec2(-0.28882018,-0.63321237),
    vec2(0.91335859,0.38749333));
float shadow_depth(vec2 coord) {
    // Outside the single Sun map is unoccluded; never wrap to an unrelated edge.
    if (any(lessThan(coord,vec2(0))) || any(greaterThan(coord,vec2(1)))) {return 1.0;}
    float edge=shadow_options.y*.5;
    return textureLod(sampler2D(shadow_texture,shadow_sampler),clamp(coord,vec2(edge),vec2(1.0-edge)),0.0).r;
}
float shadow_relative_depth(vec2 coord,vec2 offset,vec2 plane) {
    vec2 sampleCoord=coord+offset;
    if (any(lessThan(sampleCoord,vec2(0))) || any(greaterThan(sampleCoord,vec2(1)))) {return 1.0;}
    return shadow_depth(sampleCoord)-dot(plane,offset);
}
vec2 shadow_radius(float worldRadius) {
    vec2 radius=worldRadius*shadow_geometry.yz;
    return radius*min(1.0,pcss_options.z*shadow_options.y/max(max(radius.x,radius.y),.00000001));
}
// Visibility, normalized blocker count, receiver-plane-corrected linear depth, normalized radius.
vec4 shadow_result(vec3 n,vec2 plane) {
    if (shadow_options.x < .5) {return vec4(1,0,1,0);}
    vec3 p=shadow_position.xyz/shadow_position.w;vec2 coord=vec2(p.x*.5+.5,.5-p.y*.5);
    if (p.z < 0.0 || p.z > 1.0 || any(lessThan(coord,vec2(0))) || any(greaterThan(coord,vec2(1)))) {return vec4(1,0,1,0);}
    float bias=shadow_options.z+shadow_options.w*(1.0-max(dot(n,light_direction.xyz),0.0));
    float receiver=p.z-bias;
    if (shadow_options.x < 1.5 || (shadow_options.x > 2.5 && shadow_geometry.w == 0.0)) {
        float depth=shadow_depth(coord),v=receiver<=depth ? 1.0:0.0;return vec4(v,1.0-v,v>.5 ? 1.0:depth,0);
    }
    if (shadow_options.x < 2.5) {
        float sum=0.0;for (int y=-1;y<=1;++y) {for (int x=-1;x<=1;++x) {sum+=receiver<=shadow_depth(coord+vec2(float(x),float(y))*shadow_options.y) ? 1.0:0.0;}}
        return vec4(sum/9.0,0,1,0);
    }
    // Orthographic raw depth is linear. An angular source grows with separation;
    // perspective PCSS's receiver/blocker division is intentionally absent.
    vec2 search=shadow_radius(p.z*shadow_geometry.x*shadow_geometry.w);
    float count=0.0,total=0.0;
    for (int i=0;i<64;++i) {if (i>=int(pcss_options.x)) {break;}
        vec2 offset=shadow_disk[i]*search;
        float depth=shadow_relative_depth(coord,offset,plane);
        if (depth<receiver) {count+=1.0;total+=depth;}
    }
    if (count==0.0) {return vec4(1,0,1,0);}
    float average=total/count;
    vec2 radius=shadow_radius(max(p.z-average,0.0)*shadow_geometry.x*shadow_geometry.w);
    float sum=0.0;
    for (int i=0;i<64;++i) {if (i>=int(pcss_options.y)) {break;}
        vec2 offset=shadow_disk[i]*radius;
        sum+=receiver<=shadow_relative_depth(coord,offset,plane) ? 1.0:0.0;
    }
    return vec4(sum/pcss_options.y,count/pcss_options.x,average,max(radius.x,radius.y)/(shadow_options.y*pcss_options.z));
}
void main() {
    vec4 albedo=base_color*color*texture(sampler2D(base_texture,base_sampler),uv);
    vec3 geometric=normalize(world_normal);vec3 n=geometric;
    // Evaluate derivatives before dynamic branches (and support meshes without authored TANGENT).
    vec3 dp1=dFdx(world_position),dp2=dFdy(world_position);vec2 duv1=dFdx(uv),duv2=dFdy(uv);
    vec3 projected=shadow_position.xyz/shadow_position.w;
    vec3 sx=dFdx(projected),sy=dFdy(projected);
    vec2 ux=vec2(sx.x*.5,-sx.y*.5),uy=vec2(sy.x*.5,-sy.y*.5);
    float determinant=ux.x*uy.y-ux.y*uy.x;
    // Receiver-plane correction avoids classifying a tilted receiver as its own blocker.
    vec2 shadow_plane=abs(determinant)>.0000000001 ? vec2(sx.z*uy.y-sy.z*ux.y,ux.x*sy.z-uy.x*sx.z)/determinant:vec2(0);
    if (options.x >= 0.0 && albedo.a < options.x) {discard;}
    if (debug_options.x > .5 && debug_options.x < 1.5) {frag_color=vec4(albedo.rgb,1);return;}
    if (debug_options.x > 1.5 && debug_options.x < 2.5) {frag_color=vec4(1);return;}
    if (debug_options.x > 2.5 && debug_options.x < 3.5) {frag_color=vec4(geometric*.5+.5,1);return;}
    if (debug_options.x > 3.5 && debug_options.x < 4.5) {frag_color=vec4(fract(uv),0,1);return;}
    if (debug_options.x > 4.5 && debug_options.x < 5.5) {float depth=debug_options.w > .5 ? (debug_options.y+gl_FragCoord.z*(debug_options.z-debug_options.y))/debug_options.z:debug_options.y/(debug_options.z-gl_FragCoord.z*(debug_options.z-debug_options.y));frag_color=vec4(vec3(depth),1);return;}
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
    vec4 shadow=shadow_result(n,shadow_plane);
    if (debug_options.x>9.5) {
        float value=debug_options.x<10.5 ? shadow.y:debug_options.x<11.5 ? shadow.z:debug_options.x<12.5 ? shadow.w:shadow.x;
        frag_color=vec4(vec3(value),1);return;
    }
    if (pbr_options.z>.5 && options.y<.5) {
        vec2 mr=texture(sampler2D(metallic_roughness_texture,metallic_roughness_sampler),uv).gb;
        float metallic=clamp(pbr_options.x*mr.y,0.0,1.0),roughness=clamp(pbr_options.y*mr.x,.045,1.0);
        vec3 eye=debug_options.w>.5 ? view_direction.xyz:eye_position.xyz-world_position;
        vec3 v=dot(eye,eye)>0.0000001 ? normalize(eye):n;
        vec3 result=albedo.rgb*light_color.w*(1.0-metallic);
        result+=direct_brdf(albedo.rgb,metallic,roughness,n,v,light_direction.xyz)*light_color.rgb*light_direction.w*shadow.x;
        for (int i=0;i<8;++i) {if (float(i)<options.z) {
            vec3 delta=local_positions[i].xyz-world_position;float distance=length(delta);vec3 direction=delta/max(distance,.00001);
            float attenuation=pow(max(1.0-distance/max(local_positions[i].w,.00001),0.0),2.0)/(1.0+distance*distance);
            if (local_cones[i].y>.5) {float cone=dot(-direction,local_directions[i].xyz);attenuation*=clamp((cone-local_directions[i].w)/max(local_cones[i].x-local_directions[i].w,.00001),0.0,1.0);}
            result+=direct_brdf(albedo.rgb,metallic,roughness,n,v,direction)*local_colors[i].rgb*local_colors[i].w*attenuation;
        }}
        frag_color=vec4(clamp(result,vec3(0),vec3(65000)),1);return;
    }
    float diffuse=max(dot(n,light_direction.xyz),0.0);
    vec3 illumination=vec3(light_color.w)+light_color.rgb*diffuse*light_direction.w*shadow.x;
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
@program morph_mesh morph_mesh_vs mesh_fs
@program morph_skin morph_skin_vs mesh_fs
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

@vs morph_shadow_vs
@include_block morph_common
layout(binding=0) uniform shadow_params {mat4 light_mvp;};
in vec3 position;in vec4 color0;in vec2 texcoord0;
out vec2 shadow_uv;out float shadow_alpha;
void main() {vec4 p=vec4(position,1);vec3 n=vec3(0,0,1),t=vec3(1,0,0);apply_morph(p,n,t);gl_Position=light_mvp*p;shadow_uv=texcoord0;shadow_alpha=color0.a;}
@end
@vs morph_shadow_skin_vs
layout(binding=0) uniform shadow_params {mat4 light_mvp;};
@include_block skin_common
@include_block morph_common
in vec3 position;in vec4 color0;in vec2 texcoord0;
out vec2 shadow_uv;out float shadow_alpha;
void main() {
    vec4 p=vec4(position,1.0);vec3 n=vec3(0,0,1),t=vec3(1,0,0);apply_morph(p,n,t);if (skin_options.x > .5) {p=(joint_weights.x*joint_matrices[joint_indices.x]+joint_weights.y*joint_matrices[joint_indices.y]+joint_weights.z*joint_matrices[joint_indices.z]+joint_weights.w*joint_matrices[joint_indices.w])*p;}
    gl_Position=light_mvp*p;shadow_uv=texcoord0;shadow_alpha=color0.a;
}
@end
@program morph_shadow_mesh morph_shadow_vs shadow_fs
@program morph_shadow_skin morph_shadow_skin_vs shadow_fs

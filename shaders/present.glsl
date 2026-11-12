@ctype vec4 lv::detail::ShaderVec4
@vs present_vs
out vec2 uv;
void main() {
    vec2 p=vec2((gl_VertexIndex << 1) & 2,gl_VertexIndex & 2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
    uv=vec2(p.x,1.0-p.y);
}
@end
@fs present_fs
layout(binding=0) uniform texture2D rendered_texture;
layout(binding=0) uniform sampler rendered_sampler;
in vec2 uv;
out vec4 frag_color;
void main() {frag_color=texture(sampler2D(rendered_texture,rendered_sampler),uv);}
@end
@program present present_vs present_fs
@fs post_fs
layout(binding=0) uniform post_params {vec4 post_options;};
layout(binding=0) uniform texture2D rendered_texture;
layout(binding=0) uniform sampler rendered_sampler;
in vec2 uv;
out vec4 frag_color;
void main() {
    vec2 pixel=post_options.xy;vec3 luma=vec3(.299,.587,.114);
    vec3 nw=texture(sampler2D(rendered_texture,rendered_sampler),uv+vec2(-1,-1)*pixel).rgb;
    vec3 ne=texture(sampler2D(rendered_texture,rendered_sampler),uv+vec2(1,-1)*pixel).rgb;
    vec3 sw=texture(sampler2D(rendered_texture,rendered_sampler),uv+vec2(-1,1)*pixel).rgb;
    vec3 se=texture(sampler2D(rendered_texture,rendered_sampler),uv+vec2(1,1)*pixel).rgb;
    vec3 center=texture(sampler2D(rendered_texture,rendered_sampler),uv).rgb;
    if (post_options.z > .5) {frag_color=vec4(center,1);return;}
    float lnw=dot(nw,luma),lne=dot(ne,luma),lsw=dot(sw,luma),lse=dot(se,luma),lm=dot(center,luma);
    float low=min(lm,min(min(lnw,lne),min(lsw,lse))),high=max(lm,max(max(lnw,lne),max(lsw,lse)));
    vec2 dir=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
    float reduce=max((lnw+lne+lsw+lse)*.03125,.0078125);
    dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-8),vec2(8))*pixel;
    vec3 a=.5*(texture(sampler2D(rendered_texture,rendered_sampler),uv-dir/6.0).rgb+texture(sampler2D(rendered_texture,rendered_sampler),uv+dir/6.0).rgb);
    vec3 b=a*.5+.25*(texture(sampler2D(rendered_texture,rendered_sampler),uv-dir*.5).rgb+texture(sampler2D(rendered_texture,rendered_sampler),uv+dir*.5).rgb);
    float lb=dot(b,luma);vec3 filtered=(lb<low || lb>high) ? a:b;
    vec3 hdr=max(filtered,vec3(0));frag_color=vec4(hdr/(vec3(1)+hdr),1);
}
@end
@program post present_vs post_fs

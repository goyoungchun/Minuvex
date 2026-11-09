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

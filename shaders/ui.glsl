@vs ui_vs
out vec2 uv;
void main() {vec2 p=vec2((gl_VertexIndex << 1) & 2,gl_VertexIndex & 2);gl_Position=vec4(p*2.0-1.0,0,1);uv=vec2(p.x,1.0-p.y);}
@end
@fs ui_fs
layout(binding=0) uniform texture2D ui_scene;
layout(binding=1) uniform texture2D ui_overlay;
layout(binding=0) uniform sampler ui_sampler;
in vec2 uv;
out vec4 frag_color;
void main() {
    vec4 scene=texture(sampler2D(ui_scene,ui_sampler),uv);
    vec4 overlay=texture(sampler2D(ui_overlay,ui_sampler),uv);
    vec3 encoded=overlay.a>0.0 ? overlay.rgb/overlay.a : vec3(0.0);
    vec3 linear_color=mix(encoded/12.92,pow(max((encoded+0.055)/1.055,vec3(0.0)),vec3(2.4)),step(vec3(0.04045),encoded));
    overlay.rgb=linear_color*overlay.a;
    frag_color=overlay+scene*(1.0-overlay.a);
}
@end
@program ui ui_vs ui_fs

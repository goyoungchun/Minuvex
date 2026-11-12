@ctype mat4 lv::Mat4
@vs debug_line_vs
layout(binding=0) uniform debug_line_params {mat4 view_projection;};
in vec3 position;
in vec4 color0;
out vec4 color;
void main() {gl_Position=view_projection*vec4(position,1);color=color0;}
@end
@fs debug_line_fs
in vec4 color;
out vec4 frag_color;
void main() {frag_color=color;}
@end
@program debug_lines debug_line_vs debug_line_fs

@vs debug_preview_vs
out vec2 uv;
void main() {vec2 p=vec2((gl_VertexIndex << 1) & 2,gl_VertexIndex & 2);gl_Position=vec4(p*2.0-1.0,0,1);uv=vec2(p.x,1.0-p.y);}
@end
@fs debug_preview_fs
layout(binding=0) uniform texture2D debug_texture;
layout(binding=0) uniform sampler debug_sampler;
in vec2 uv;
out vec4 frag_color;
void main() {float depth=textureLod(sampler2D(debug_texture,debug_sampler),uv,0).r;frag_color=vec4(vec3(depth),1);}
@end
@program debug_preview debug_preview_vs debug_preview_fs

// vim: set commentstring=//\ %s:
// Copied and modified from this file, which distributed under MIT License.
// https://github.com/floooh/sokol-samples/blob/801de1f6ef8acc7f824efe259293eb88a4476479/sapp/quad-sapp.glsl
#version 300

@vs vs
in vec4 position;
in vec4 color0;
out vec4 color;

void main() {
    gl_Position = position;
    color = color0;
}
@end

@fs fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program quad vs fs

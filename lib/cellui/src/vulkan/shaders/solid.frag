#version 450

layout(push_constant) uniform Push {
  vec4 colour;
} push;

layout(location = 0) out vec4 out_colour;

void main() {
  out_colour = push.colour;
}

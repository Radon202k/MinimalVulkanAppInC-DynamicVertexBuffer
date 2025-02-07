#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;

layout(set = 0, binding = 1) uniform UniformBufferObject
{
    layout(row_major) mat4 projection;
} ubo;

void main()
{
    gl_Position = ubo.projection * vec4(inPosition, 0.0, 1.0);
    outUV = inUV;
}
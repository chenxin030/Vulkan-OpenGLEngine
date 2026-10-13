#version 450

uniform mat4 uModel;
uniform mat4 uLightViewProjection;
layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = uLightViewProjection * uModel * vec4(inPosition, 1.0);
}

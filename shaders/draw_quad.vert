#version 450 core
layout(location=0) in vec2 coord;

void main(void) 
{
	gl_Position = vec4(coord, 0.0, 1.0);
}
#version 450 core
readonly restrict uniform layout(rgba16f) image2D image;
uniform uint samples;
layout(location=0) out vec4 color;

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main(void) 
{
	vec3 col3 = vec3(0.0);
	for(uint i = 0; i < samples; i++)
	{
		col3 += imageLoad(image, ivec2(gl_FragCoord.xy) * ivec2(samples, 1) + ivec2(i, 0)).xyz;
	}
	col3 /= samples;
	color = vec4(ACESFilm(col3), 1.0);
}
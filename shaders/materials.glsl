// from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
// why does pixar have an 8 page paper on calculating an ONB
void ONB(const vec3 n, out vec3 b1, out vec3 b2)
{
    const float sign = n.z < 0.0 ? -1.0 : 1.0; // sign(n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = vec3(b, sign + n.y * n.y * a, -n.y);
}

// surface brdf functions
vec3 lambertian(vec3 color)
{
    return color * invPi;
}

float lambertianPDF(float wiDotN)
{
    return invPi * wiDotN;
}

vec3 sampleLambertian(vec3 n, vec2 uv)
{
    vec3 b1, b2;
    ONB(n, b1, b2);

    float r = sqrt(uv.x);
    float theta = 2 * pi * uv.y;

    float x = r * cos(theta);
    float y = r * sin(theta);

    vec3 s = vec3(x, y, sqrt(max(0.0, 1 - uv.x)));
    
    /*float theta = acos(sqrt(uv.x));
    float phi = twoPi * uv.y;
    vec3 s = vec3(
        cos(phi) * sin(theta),
        sin(phi) * sin(theta),
        cos(theta)
    );*/
    return normalize(s.x * b1 + s.y * b2 + s.z * n);
}

// clearcoat
// https://schuttejoe.github.io/post/disneybsdf/
float pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Fresnel(float F0, float cosA)
{
    return F0 + (1 - F0) * pow5(1 - cosA);
}

float GTR1(float absDotHL, float a)
{
    if (a >= 1) {
        return invPi;
    }

    float a2 = a * a;
    return (a2 - 1.0) / (pi * log2(a2) * (1.0 + (a2 - 1.0) * absDotHL * absDotHL));
}

float SeparableSmithGGXG1(vec3 w, vec3 n, float a)
{
    float a2 = a * a;
    float absDotNV = abs(dot(w, n));

    return 2.0 / (1.0 + sqrt(a2 + (1 - a2) * absDotNV * absDotNV));
}

float EvaluateDisneyClearcoat(float clearcoat, float alpha, vec3 wo, vec3 wm, vec3 wi, vec3 n, 
    out float d)
{
    if (clearcoat <= 0.0) {
        return 0.0;
    }

    float absDotNH = dot(wm, n);
    float absDotNL = dot(wi, n);
    float absDotNV = dot(wo, n);
    float dotHL = dot(wm, wi);

    d = GTR1(absDotNH, mix(0.1, 0.001, alpha));
    float f = Fresnel(0.04, dotHL);
    float gl = SeparableSmithGGXG1(wi, n, 0.25);
    float gv = SeparableSmithGGXG1(wo, n, 0.25);

    return 0.25 * clearcoat * d * f * gl * gv;
}

float clearcoatPDF(float d, float absDotMV)
{
    return d / (4.0 * absDotMV);
}

vec3 SampleDisneyClearcoat(vec3 wo, vec3 n, out vec3 wm, float alpha, vec2 uv)
{
    float a2 = alpha * alpha;

    float r0 = uv.x;
    float r1 =uv.y;
    float cosTheta = sqrt(max(0.0, (1.0 - pow(a2, 1.0 - r0)) / (1.0 - a2)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = twoPi * r1;

    wm = vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));

    if (dot(wm, wo) < 0.0)
    {
        wm = -wm;
    }

    return reflect(-wm, n);
}

// volume phase function
vec3 schlickPhase(vec3 color, vec3 wo, vec3 wi, float k, out float pdf)
{
    float cosTheta = dot(wi, -wo);
    float a = (1 + k * cosTheta);
    pdf = invFourPi;
    return color * (1 - k * k) / (4 * pi * a * a);
}

vec3 sampleSchlickPhase(vec3 wi, vec2 uv)
{
    float theta = twoPi * uv.x;
    float phi = acos(2.0 * uv.y - 1.0);

    return vec3(
        sin(phi) * cos(theta),
        sin(phi) * sin(theta),
        cos(phi)
    );
}

// probability that brdf will be used vs phase function
float pBRDF(float opacity, float gradMag, float g)
{
    return opacity * (1.0 - pow(10.0, -25 * g * g * g * gradMag));
}
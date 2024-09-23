#ifndef raycommon
#define raycommon

const float M_PI            = 3.1415926535897F;         // PI
const float M_TWO_PI        = 6.2831853071795F;         // 2*PI
const float M_PI_2          = 1.5707963267948F;         // PI/2
const float M_PI_4          = 0.7853981633974F;         // PI/4
const float M_1_OVER_PI     = 0.3183098861837F;         // 1/PI
const float M_2_OVER_PI     = 0.6366197723675F;         // 2/PI
const float INV_M_PI        = 0.31830988618379067153f;  /* 1/pi */
const float SQRT_M_PI       = 1.77245385090551602729f;  /* sqrt(pi) */
const float SQRT_2          = 1.41421356237309504880f;  /* sqrt(2) */
const float INV_SQRT_M_PI   = 0.56418958354775628694f;  /* 1/sqrt(pi) */
const float INV_2_SQRT_M_PI = 0.28209479177387814347f;  /* 0.5/sqrt(pi) */
const float INV_SQRT_2_M_PI = 0.3989422804014326779f;   /* 1/sqrt(2*pi) */
const float INV_SQRT_2      = 0.7071067811865475244f;   /* 1/sqrt(2) */
const float FLT_MAX         = 3.402823466e+38F;         // max value

const int DEPTH_INFINITE = 100000;

struct hitPayload
{
    vec3 HitValue;
    uint Seed;
    uint Depth;
    vec3 RayOrigin;
    vec3 RayDirection;
    vec3 Weight;
    bool MissedAllGeometry;

    bool InMedium;
    uint MediumID;

    vec3 MediumColor;
    float MediumDensity;
    float MediumAnisotropy;
};

struct GlobalUniforms
{
    mat4 ViewProjectionMat;
    mat4 ViewInverse;
    mat4 ProjInverse;
};

#extension GL_EXT_shader_explicit_arithmetic_types_float64 : enable

struct PushConstantRay
{
    uint64_t Frame;
    int MaxDepth;
    int SamplesPerFrame;
    float EnvAzimuth;
    float EnvAltitude;

    float FocalLenght;
    float DoFStrenght;
    float AliasingJitter;
    float CausticsLuminance;
    int VolumesCount;
};

struct Vertex
{
    vec3 Position;
    vec3 Normal;
    vec2 TexCoord;
};

struct MeshAdresses
{
    uint64_t VertexBuffer;
    uint64_t IndexBuffer;
};

struct MaterialLoad
{
    vec4 Color;
    vec4 EmissiveColor;
    vec4 MediumColor;

    float Metallic;
    float Roughness;
    float SpecularTint;

    float Ior;
    float Transparency;
    float MediumDensity;
    float MediumAnisotropy;

    float Anisotropy;
    float AnisotropyRotation;
};

struct Material
{
    vec4 Color;
    vec4 EmissiveColor;
    vec4 MediumColor;

    float Metallic;
    float Roughness;
    float SpecularTint;

    float Ior;
    float Transparency;
    float MediumDensity;
    float MediumAnisotropy;

    float Anisotropy;
    float AnisotropyRotation;

    float eta;

    float ax;
    float ay;
};

struct Surface
{
    vec3 Normal;
    vec3 Tangent;
    vec3 Bitangent;

    vec3 GeoNormal;
    vec3 NormalNoTex;
};

struct AABB
{
    vec3 Min;
    vec3 Max;
};

struct HitData
{
    float HitDistance;
};

void IntersectRayAABB(vec3 rayOrigin, vec3 rayDirection, AABB aabb, out vec3 hitPointNear, out vec3 hitPointFar) 
{
    vec3 tMin = (aabb.Min - rayOrigin) / rayDirection;
    vec3 tMax = (aabb.Max - rayOrigin) / rayDirection;

    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);

    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);

    if (tNear > tFar || tFar < 0.0f) {
        // No intersection
        hitPointNear = vec3(-1.0f);
        hitPointFar = vec3(-1.0f);
        return;
    }

    // Ray intersects AABB
    hitPointNear = rayOrigin + rayDirection * tNear;
    hitPointFar = rayOrigin + rayDirection * tFar;
}

bool IsInsideAABB(vec3 point, AABB aabb)
{
    if (
        (aabb.Min.x <= point.x && aabb.Max.x >= point.x) &&
        (aabb.Min.y <= point.y && aabb.Max.y >= point.y) &&
        (aabb.Min.z <= point.z && aabb.Max.z >= point.z)
        )
    {
        return true;
    }
    return false;
}

struct Volume
{
    AABB Aabb;
    vec3 Color;
    float ScatteringCoefficient;
    float AbsorptionCoefficient;
    float G;
};

struct EnvAccel
{
    uint Alias;
    float Importance;
};

uint PCG(inout uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    seed = (word >> 22u) ^ word;
    return seed & 0x00FFFFFF;
}

bool IsFiniteNumber(float x)
{
    return (x <= FLT_MAX && x >= -FLT_MAX);
}

float Rnd(inout uint prev)
{
    return (float(PCG(prev)) / float(0x01000000));
}

vec3 Rotate(vec3 v, vec3 k, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    return (v * cosTheta) + (cross(k, v) * sinTheta) + (k * dot(k, v)) * (1.0F - cosTheta);
}

vec2 directionToSphericalEnvmap(vec3 v)
{
    float gamma = asin(-v.y);
    float theta = atan(v.z, v.x);

    vec2 uv = vec2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
    return uv;
}

vec3 sphericalEnvmapToDirection(vec2 tex)
{
    float theta = M_PI * (1.0 - tex.t);
    float phi = 2.0 * M_PI * (0.5 - tex.s);
    return vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
}

// Return the tangent and binormal from the incoming normal
void CreateCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb)
{
    if(abs(N.x) > abs(N.y))
        Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
    else
        Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
    Nb = cross(N, Nt);
}

vec2 RandomPointInCircle(inout uint seed)
{
    float angle = Rnd(seed) * 2.0 * M_PI;
    vec2 pointOnCircle = vec2(cos(angle), sin(angle));
    return pointOnCircle * sqrt(Rnd(seed));
}

vec2 RandomPointInSquare(inout uint seed)
{
    return vec2(Rnd(seed) * 2.0 - 1.0f, Rnd(seed) * 2.0 - 1.0f);
}

float GetLuminance(vec3 color)
{
    return color.r * 0.212671f + color.g * 0.715160f + color.b * 0.072169f;
}

vec3 OffsetRay(in vec3 p, in vec3 n)
{
    // Smallest epsilon that can be added without losing precision is 1.19209e-07, but we play safe
    const float epsilon = 1e-4f;  // Safe epsilon
    
    float magnitude = length(p);
    float offset = epsilon * magnitude;
    // multiply the direction vector by the smallest offset
    vec3 offsetVector = n * offset;
    // add the offset vector to the starting point
    vec3 offsetPoint = p + offsetVector;

    return offsetPoint;
}

float CalculateLuminance(vec3 rgb)
{
    return 0.212671f * rgb.r + 0.715160f * rgb.g + 0.072169f * rgb.b;
}

void CalculateTangents(in vec3 N, out vec3 T, out vec3 B)
{
    vec3 up = abs(N.z) < 0.9999999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

bool IsNan(vec3 vec)
{
    if (isnan(vec).x || isnan(vec).y || isnan(vec).z)
        return true;
    return false;
}

vec3 TangentToWorld(vec3 T, vec3 B, vec3 N, vec3 V)
{
    return normalize(V.x * T + V.y * B + V.z * N);
}

vec3 WorldToTangent(vec3 T, vec3 B, vec3 N, vec3 V)
{
    return normalize(vec3(dot(V, T), dot(V, B), dot(V, N)));
}

vec3 SampleHenyeyGreenstein(float g, vec3 incidentDir, vec2 rand)
{
    float cosTheta;
    if (abs(g) < 1e-5) {
        cosTheta = 2.0 * rand.x - 1.0f;
    }
    else {
        float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * rand.x);
        cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
    }

    float phi = 2.0 * M_PI * rand.y;

    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 newDir = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    vec3 tangent;
    vec3 bitangent;
    CalculateTangents(incidentDir, tangent, bitangent);

    vec3 scatteredDir = TangentToWorld(tangent, bitangent, incidentDir, newDir);

    return scatteredDir;
}

// Perlin Noise
// source: https://github.com/stegu/webgl-noise

vec3 mod289(vec3 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 mod289(vec4 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 permute(vec4 x)
{
    return mod289(((x * 34.0) + 10.0) * x);
}

vec4 taylorInvSqrt(vec4 r)
{
    return 1.79284291400159 - 0.85373472095314 * r;
}

vec3 fade(vec3 t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Classic Perlin noise
float cnoise(vec3 P)
{
    vec3 Pi0 = floor(P); // Integer part for indexing
    vec3 Pi1 = Pi0 + vec3(1.0); // Integer part + 1
    Pi0 = mod289(Pi0);
    Pi1 = mod289(Pi1);
    vec3 Pf0 = fract(P); // Fractional part for interpolation
    vec3 Pf1 = Pf0 - vec3(1.0); // Fractional part - 1.0
    vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    vec4 iy = vec4(Pi0.yy, Pi1.yy);
    vec4 iz0 = Pi0.zzzz;
    vec4 iz1 = Pi1.zzzz;

    vec4 ixy = permute(permute(ix) + iy);
    vec4 ixy0 = permute(ixy + iz0);
    vec4 ixy1 = permute(ixy + iz1);

    vec4 gx0 = ixy0 * (1.0 / 7.0);
    vec4 gy0 = fract(floor(gx0) * (1.0 / 7.0)) - 0.5;
    gx0 = fract(gx0);
    vec4 gz0 = vec4(0.5) - abs(gx0) - abs(gy0);
    vec4 sz0 = step(gz0, vec4(0.0));
    gx0 -= sz0 * (step(0.0, gx0) - 0.5);
    gy0 -= sz0 * (step(0.0, gy0) - 0.5);

    vec4 gx1 = ixy1 * (1.0 / 7.0);
    vec4 gy1 = fract(floor(gx1) * (1.0 / 7.0)) - 0.5;
    gx1 = fract(gx1);
    vec4 gz1 = vec4(0.5) - abs(gx1) - abs(gy1);
    vec4 sz1 = step(gz1, vec4(0.0));
    gx1 -= sz1 * (step(0.0, gx1) - 0.5);
    gy1 -= sz1 * (step(0.0, gy1) - 0.5);

    vec3 g000 = vec3(gx0.x, gy0.x, gz0.x);
    vec3 g100 = vec3(gx0.y, gy0.y, gz0.y);
    vec3 g010 = vec3(gx0.z, gy0.z, gz0.z);
    vec3 g110 = vec3(gx0.w, gy0.w, gz0.w);
    vec3 g001 = vec3(gx1.x, gy1.x, gz1.x);
    vec3 g101 = vec3(gx1.y, gy1.y, gz1.y);
    vec3 g011 = vec3(gx1.z, gy1.z, gz1.z);
    vec3 g111 = vec3(gx1.w, gy1.w, gz1.w);

    vec4 norm0 = taylorInvSqrt(vec4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 *= norm0.x;
    g010 *= norm0.y;
    g100 *= norm0.z;
    g110 *= norm0.w;
    vec4 norm1 = taylorInvSqrt(vec4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 *= norm1.x;
    g011 *= norm1.y;
    g101 *= norm1.z;
    g111 *= norm1.w;

    float n000 = dot(g000, Pf0);
    float n100 = dot(g100, vec3(Pf1.x, Pf0.yz));
    float n010 = dot(g010, vec3(Pf0.x, Pf1.y, Pf0.z));
    float n110 = dot(g110, vec3(Pf1.xy, Pf0.z));
    float n001 = dot(g001, vec3(Pf0.xy, Pf1.z));
    float n101 = dot(g101, vec3(Pf1.x, Pf0.y, Pf1.z));
    float n011 = dot(g011, vec3(Pf0.x, Pf1.yz));
    float n111 = dot(g111, Pf1);

    vec3 fade_xyz = fade(Pf0);
    vec4 n_z = mix(vec4(n000, n100, n010, n110), vec4(n001, n101, n011, n111), fade_xyz.z);
    vec2 n_yz = mix(n_z.xy, n_z.zw, fade_xyz.y);
    float n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x);
    return 2.2 * n_xyz;
}

#else
#endif
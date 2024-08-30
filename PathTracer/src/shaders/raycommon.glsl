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
    float Metallic;
    float Roughness;
    float SpecularTint;

    float Ior;
    float Transparency;

    float Anisotropy;
};

struct Material
{
    vec4 Color;
    vec4 EmissiveColor;
    float Metallic;
    float Roughness;
    float SpecularTint;

    float Ior;
    float Transparency;

    float Anisotropy;

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

float GetLuminance(vec3 color)
{
    return color.r * 0.212671f + color.g * 0.715160f + color.b * 0.072169f;
}

vec3 OffsetRay(in vec3 p, in vec3 n)
{
    // Smallest epsilon that can be added without losing precision is 1.19209e-07, but we play safe
    const float epsilon = 1.0f / 65536.0f;  // Safe epsilon
    
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
    return V.x * T + V.y * B + V.z * N;
}

vec3 WorldToTangent(vec3 T, vec3 B, vec3 N, vec3 V)
{
    return vec3(dot(V, T), dot(V, B), dot(V, N));
}

#else
#endif
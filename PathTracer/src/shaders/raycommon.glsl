#ifndef raycommon
#define raycommon

const float M_PI = 3.1415926535897F;  // PI
const float M_TWO_PI = 6.2831853071795F;  // 2*PI
const float M_PI_2 = 1.5707963267948F;  // PI/2
const float M_PI_4 = 0.7853981633974F;  // PI/4
const float M_1_OVER_PI = 0.3183098861837F;  // 1/PI
const float M_2_OVER_PI = 0.6366197723675F;  // 2/PI

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

    float Pdf;
    vec3 Bsdf;
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
    int64_t Frame;
    int MaxDepth;
    int SamplesPerFrame;
    float EnvAzimuth;
    float EnvAltitude;

    float FocalLenght;
    float DoFStrenght;
    float AliasingJitter;
};

struct Vertex
{
    vec3 Position;
    vec3 Normal;
    vec3 Tangent;
    vec3 Bitangent;
    vec2 TexCoord;
};

struct MeshAdresses
{
    uint64_t VertexBuffer;
    uint64_t IndexBuffer;
};

#define MEDIUM_ABSORB 1;
#define MEDIUM_NONE 0;

struct Material
{
    vec4 Albedo;
    vec4 Emissive;
    float Metallic;
    float Roughness;

    float Ior;
    float SpecTrans;
    float Clearcoat;
    float ClearcoatRoughness;
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

vec3 CosineSamplingHemisphere(inout uint seed, in vec3 tangent, in vec3 bitangent, in vec3 normal)
{
    float r1 = Rnd(seed);
    float r2 = Rnd(seed);
    float sq = sqrt(r1);

    const float phi = 2 * M_PI * r2;
    vec3 direction = vec3(cos(phi) * sq, sin(phi) * sq, sqrt(1.0f - r1));
    //direction = direction.x * tangent + direction.y * bitangent + direction.z * normal;

    if (dot(direction, normal) < 0.0)
        direction = -direction;

    return direction;
}

vec3 UniformSamplingHemisphere(inout uint seed, in vec3 tangent, in vec3 bitangent, in vec3 normal)
{
#if 0
    float r1 = Rnd(seed);
    float r2 = Rnd(seed);

    const float phi = 2 * M_PI * r1;

    vec3 direction;
    direction.x = cos(phi) * 2 * sqrt(r2 * (1 - r2));
    direction.y = sin(phi) * 2 * sqrt(r2 * (1 - r2));
    direction.z = 1 - 2 * r2;

    direction = direction.x * tangent + direction.y * bitangent + direction.z * normal;
    //if (dot(direction, normal) < 0.0)
    //    direction = -direction;

    return direction;
#else
    float r1 = Rnd(seed) * 2.0f - 1.0f;
    float r2 = Rnd(seed) * 2.0f - 1.0f;
    float r3 = Rnd(seed) * 2.0f - 1.0f;

    vec3 direction = normalize(vec3(r1, r2, r3));
    if (dot(direction, normal) < 0.0)
        direction = -direction;
    //direction = direction.x * tangent + direction.y * bitangent + direction.z * normal;

    return direction;
#endif
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
    return color.r * 0.2126F + color.g * 0.7152F + color.b * 0.0722F;
}

vec3 Slerp(vec3 p0, vec3 p1, float t)
{
    float dotp = dot(normalize(p0), normalize(p1));
    if ((dotp > 0.9999) || (dotp < -0.9999))
    {
        if (t <= 0.5)
            return p0;
        return p1;
    }
    float theta = acos(dotp);
    vec3 P = ((p0 * sin((1 - t) * theta) + p1 * sin(t * theta)) / sin(theta));
    return P;
}

vec3 OffsetRay(in vec3 p, in vec3 n)
{
    //// Smallest epsilon that can be added without losing precision is 1.19209e-07, but we play safe
    //const float epsilon = 1.0f / 65536.0f * 5.0f;  // Safe epsilon
    //
    //float magnitude = length(p);
    //float offset = epsilon * magnitude;
    //// multiply the direction vector by the smallest offset
    //vec3 offsetVector = n * offset;
    //// add the offset vector to the starting point
    //vec3 offsetPoint = p + offsetVector;

    return p;
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
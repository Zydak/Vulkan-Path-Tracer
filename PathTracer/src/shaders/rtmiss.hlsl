static const int DEPTH_INFINITE = 100000;
static const float M_PI              = 3.1415926535897F;         // PI
static const float M_2_PI            = 6.2831853071795F;         // 2*PI
static const float M_PI_2            = 1.5707963267948F;         // PI/2
static const float M_PI_4            = 0.7853981633974F;         // PI/4
static const float M_1_OVER_PI       = 0.3183098861837F;         // 1/PI
static const float M_2_OVER_PI       = 0.6366197723675F;         // 2/PI
static const float INV_M_PI          = 0.31830988618379067153f;  /* 1/pi */
static const float SQRT_M_PI         = 1.77245385090551602729f;  /* sqrt(pi) */
static const float SQRT_2            = 1.41421356237309504880f;  /* sqrt(2) */
static const float INV_SQRT_M_PI     = 0.56418958354775628694f;  /* 1/sqrt(pi) */
static const float INV_2_SQRT_M_PI   = 0.28209479177387814347f;  /* 0.5/sqrt(pi) */
static const float INV_SQRT_2_M_PI   = 0.3989422804014326779f;   /* 1/sqrt(2*pi) */
static const float INV_SQRT_2        = 0.7071067811865475244f;   /* 1/sqrt(2) */

static const float FLT_MAX           = 3.402823466e+38F; // max value
static const uint UINT_MAX           = 4294967295;

struct RTPayload
{
    float3 HitValue;
    float3 RayOrigin;
    float3 RayDirection;
    float3 Weight;

    float3 SurfaceAlbedo;
    float3 SurfaceNormal;

    uint Sampler;
    uint Depth;
};

struct PushConstant
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

struct Input
{
    [[vk::location(0)]] RTPayload Payload;
};

[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D uEnvironmentMapTexture;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState uEnvironmentMapSampler;

[[vk::push_constant]] PushConstant push;

float2 TexCoordsToSpherical(float2 texCoords)
{
    float phi = (texCoords.x - 0.5F) * 2.0F * M_PI; // Maps to the range [-PI, PI]
    float theta = M_PI - texCoords.y * M_PI;               // Maps to the range [0, PI] (0 = south pole, PI = north pole)

    return float2(theta, phi);
}

float2 SphericalToTexCoords(float theta, float phi)
{
    float u = (phi * 0.5F * M_1_OVER_PI) + 0.5F; // Maps phi from [-PI, PI] to [0, 1]
    float v = (M_PI - theta) * M_1_OVER_PI;      // Maps theta from [0, PI] to [0, 1]

    return float2(u, v);
}

float2 DirectionToSpherical(float3 v)
{
    float theta = acos(v.z);
    float phi = atan2(v.y, v.x);

    return float2(theta, phi);
}

float3 SphericalToDirection(float theta, float phi)
{
    float x = sin(theta) * cos(phi);
    float y = sin(theta) * sin(phi);
    float z = cos(theta);

    return float3(x, y, z);
}

[shader("miss")]
void main(inout Input input)
{
    float3 rayDir = input.Payload.RayDirection;
    float3 color;
    //rayDir = Rotate(rayDir, float3(1, 0, 0), M_PI_2);
    //
    //rayDir = Rotate(rayDir, float3(0, 0, 1), push.EnvAzimuth);
    //rayDir = Rotate(rayDir, float3(1, 0, 0), push.EnvAltitude);

    uint2 textureSize;
    uint mipLevels;
    uEnvironmentMapTexture.GetDimensions(0, textureSize.x, textureSize.y, mipLevels);

    float2 sphericalCoords = DirectionToSpherical(rayDir);

    float2 texCoords = SphericalToTexCoords(sphericalCoords.x, sphericalCoords.y);
    uint2 texCoordsInt = uint2(texCoords * textureSize);

    color = uEnvironmentMapTexture.SampleLevel(uEnvironmentMapSampler, texCoords, 0).rgb;
    //color = uEnvironmentMapTexture.Load(float3(texCoordsInt, 0)).rgb;

    input.Payload.HitValue = color;
    input.Payload.Depth = DEPTH_INFINITE;
    input.Payload.SurfaceAlbedo = color;
    input.Payload.SurfaceNormal = float3(0.0f, 0.0f, 0.0f);
}
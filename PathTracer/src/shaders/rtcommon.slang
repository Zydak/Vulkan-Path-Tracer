module rtcommon;

public static const float M_PI             = 3.1415926535897F;         // PI
public static const float M_2_PI           = 6.2831853071795F;         // 2*PI
public static const float M_PI_2           = 1.5707963267948F;         // PI/2
public static const float M_PI_4           = 0.7853981633974F;         // PI/4
public static const float M_1_OVER_PI      = 0.3183098861837F;         // 1/PI
public static const float M_2_OVER_PI      = 0.6366197723675F;         // 2/PI
public static const float INV_M_PI         = 0.31830988618379067153f;  /* 1/pi */
public static const float SQRT_M_PI        = 1.77245385090551602729f;  /* sqrt(pi) */
public static const float SQRT_2           = 1.41421356237309504880f;  /* sqrt(2) */
public static const float INV_SQRT_M_PI    = 0.56418958354775628694f;  /* 1/sqrt(pi) */
public static const float INV_2_SQRT_M_PI  = 0.28209479177387814347f;  /* 0.5/sqrt(pi) */
public static const float INV_SQRT_2_M_PI  = 0.3989422804014326779f;   /* 1/sqrt(2*pi) */
public static const float INV_SQRT_2       = 0.7071067811865475244f;   /* 1/sqrt(2) */
public static const float FLT_MAX          = 3.402823466e+38F;         // max value

public static const int DEPTH_INFINITE = 100000;

public struct PushConstant
{
    public uint64_t Frame;
    public int MaxDepth;
    public int SamplesPerFrame;
    public float EnvAzimuth;
    public float EnvAltitude;

    public float FocalLenght;
    public float DoFStrenght;
    public float AliasingJitter;
    public float CausticsLuminance;
    public int VolumesCount;
};

public enum LastEvent
{
    Volume,
    Surface
};

public struct HitPayload
{
    public float3 HitValue;
    public float3 MediumColor;
    public float3 RayOrigin;
    public float3 RayDirection;
    public float3 Weight;

    public float3 SurfaceAlbedo;
    public float3 SurfaceNormal;

    public uint Seed;
    public uint Depth;

    public LastEvent LastEvent;

    public uint MediumID;

    public float MediumDensity;
    public float MediumAnisotropy;

    public bool InMedium;
    public bool MissedAllGeometry;
};

public float3 Rotate(float3 v, float3 axis, float theta)
{
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    float3 normalizedAxis = normalize(axis);

    return (v * cosTheta) + (cross(normalizedAxis, v) * sinTheta) + (normalizedAxis * dot(normalizedAxis, v)) * (1.0f - cosTheta);
}

public float2 TexCoordsToSpherical(float2 texCoords)
{
    float phi = (texCoords.x - 0.5F) * 2.0F * M_PI; // Maps to the range [-PI, PI]
    float theta = M_PI - texCoords.y * M_PI;               // Maps to the range [0, PI] (0 = south pole, PI = north pole)

    return float2(theta, phi);
}

public float2 SphericalToTexCoords(float theta, float phi)
{
    float u = (phi * 0.5F * M_1_OVER_PI) + 0.5F; // Maps phi from [-PI, PI] to [0, 1]
    float v = (M_PI - theta) * M_1_OVER_PI;      // Maps theta from [0, PI] to [0, 1]

    return float2(u, v);
}

public float2 DirectionToSpherical(float3 v)
{
    float theta = acos(v.z);
    float phi = atan2(v.y, v.x);

    return float2(theta, phi);
}

public float3 SphericalToDirection(float theta, float phi)
{
    float x = sin(theta) * cos(phi);
    float y = sin(theta) * sin(phi);
    float z = cos(theta);

    return float3(x, y, z);
}
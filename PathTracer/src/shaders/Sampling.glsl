#ifndef Sampling
#define Sampling


// -----------------------------------------------------------------------------------------------------------------
// GGX
// 
// Based on Sampling the GGX Distribution of Visible Normals 2018 https://jcgt.org/published/0007/04/01/paper.pdf

float GGXDistributionAnisotropic(vec3 H, float ax, float ay)
{
    float Hx2 = H.x * H.x;
    float Hy2 = H.y * H.y;
    float Hz2 = H.z * H.z;

    float ax2 = ax * ax;
    float ay2 = ay * ay;

    return 1.0f / M_PI * ax * ay * pow(Hx2 / ax2 + Hy2 / ay2 + Hz2, 2.0f);
}

float Lambda(vec3 V, float ax, float ay)
{
    float Vx2 = V.x * V.x;
    float Vy2 = V.y * V.y;
    float Vz2 = abs(V.z) * abs(V.z);

    float ax2 = ax * ax;
    float ay2 = ay * ay;

    float nominator = -1.0f + sqrt(1.0f + (ax2 * Vx2 + ay2 * Vy2) / Vz2);

    return nominator / 2.0f;
}

float GGXSmithAnisotropic(vec3 V, float ax, float ay)
{
    return 1.0f / (1.0f + Lambda(V, ax, ay));
}

vec3 GGXSampleAnisotopic(vec3 Ve, float ax, float ay, float u1, float u2)
{
    vec3 Vh = normalize(vec3(ax * Ve.x, ay * Ve.y, abs(Ve.z)));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
    vec3 T2 = cross(Vh, T1);

    float r = sqrt(u1);
    float phi = 2.0 * M_PI * u2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    vec3 Ne = normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));

    return Ne;
}

// GGX
// 
// -----------------------------------------------------------------------------------------------------------------


// -----------------------------------------------------------------------------------------------------------------
// Hemishphere
//


vec3 RandomVec(inout uint seed)
{
    vec3 vec;
    vec.x = Rnd(seed) * 2.0f - 1.0f;
    vec.y = Rnd(seed) * 2.0f - 1.0f;
    vec.z = Rnd(seed) * 2.0f - 1.0f;

    return vec;
}

vec3 RandomSphereVec(inout uint seed)
{
    // Spherical Coordinates

    float theta = 2.0f * M_PI * Rnd(seed);
    float phi = acos(2.0f * Rnd(seed) - 1.0f);

    vec3 dir;
    dir.x = sin(phi) * cos(theta);
    dir.y = sin(phi) * sin(theta);
    dir.z = cos(phi);

    return dir;
}

vec3 UniformSamplingHemisphere(inout uint seed, in vec3 normal)
{
    vec3 direction = normalize(RandomSphereVec(seed));
    if (dot(direction, normal) < 0.0)
        direction = -direction;

    return direction;
}

vec3 CosineSamplingHemisphere(inout uint seed, in vec3 normal)
{
    vec3 direction = normalize(RandomSphereVec(seed));

    // Case where direction is equal to -normal should be handled but I 
    // delete all of the nans later on anyway so who cares, it's always one less if statement

    return normalize(direction + normal);
}

//
// Hemishphere
// -----------------------------------------------------------------------------------------------------------------

#endif
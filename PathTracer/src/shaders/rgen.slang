import rtcommon;
import Camera;
import Ray;
import Film;
import Payload;
import Sampler;
import Volume;

[[vk::binding(1, 1)]] Sampler2D uEnvironmentMap;
[[vk::binding(2, 1)]] StructuredBuffer<EnvAccel> uAccels;

[[vk::binding(0, 0)]] RaytracingAccelerationStructure uTopLevelAS;
[[vk::binding(0, 1)]] ConstantBuffer<GlobalUniform> uUniformBuffer;

[[vk::image_format("rgba32f")]]
[[vk::binding(1, 0)]] RWTexture2D<float4> uImage;
[[vk::binding(8, 0)]] RWStructuredBuffer<float> uDof;

[[vk::binding(12, 0)]] StructuredBuffer<Volume> uVolumes;

[[vk::binding(14, 0)]] RWTexture2D<float4> uGBufferAlbedo;
[[vk::binding(15, 0)]] RWTexture2D<float4> uGBufferNormal;

[[vk::push_constant]] PushConstant push;

void EnumerateThroughVolumes(in const Ray ray, inout Sampler sampler, out float outClosestScatterDistance, out float outPathLengthThroughVolume, out Volume outFinalVolume)
{
    float closestGeometry = -1.0;
    if (push.VolumesCount > 0)
    {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray.GetOrigin(), 0.0001, ray.GetDirection(), 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            closestGeometry = query.CommittedRayT();
        }
    }

    outClosestScatterDistance = FLT_MAX;
    for (int v = 0; v < push.VolumesCount; v++)
    {
        float3 hitPosNear;
        float3 hitPosFar;
        Volume volume = uVolumes[v];
        volume.Aabb.IntersectRay(ray.GetOrigin(), ray.GetDirection(), hitPosNear, hitPosFar);

        if (volume.Aabb.IsInsideAABB(ray.GetOrigin()))
            hitPosNear = ray.GetOrigin();

        if (!VectorEqual(hitPosNear, FLT_MAX) && !VectorEqual(hitPosFar, FLT_MAX))
        {
            float distanceToNear = length(ray.GetOrigin() - hitPosNear);
            float distanceToFar = length(ray.GetOrigin() - hitPosFar);
            float pathLengthThroughVolume;

            if (closestGeometry != -1.0)
            {
                // Geometry is in front of the volume
                if (distanceToNear > closestGeometry)
                {
                    pathLengthThroughVolume = 0.0;
                }
                // Geometry is splitting the volume
                else if (distanceToFar > closestGeometry)
                {
                    pathLengthThroughVolume = closestGeometry - distanceToNear;
                }
                // Volume is in front of the geometry
                else if (distanceToFar < closestGeometry)
                {
                    pathLengthThroughVolume = distanceToFar - distanceToNear;
                }
            }
            else
            {
                pathLengthThroughVolume = distanceToFar - distanceToNear;
            }

            if (closestGeometry == -1.0 || distanceToNear < closestGeometry)
            {
                float scatterDistance = volume.SampleScatterDistance(sampler);
                float scatterDistanceFromCamera = (distanceToNear + scatterDistance);

                if (scatterDistance < pathLengthThroughVolume && pathLengthThroughVolume > 0.0)
                {
                    if (scatterDistanceFromCamera < outClosestScatterDistance)
                    {
                        outPathLengthThroughVolume = pathLengthThroughVolume;
                        outClosestScatterDistance = scatterDistanceFromCamera;
                        outFinalVolume = volume;
                    }
                }
            }
        }
    }
}

[shader("raygeneration")]
void main()
{
    Film film = Film(uImage);
    uint3 LaunchID = DispatchRaysIndex();
    uint3 LaunchSize = DispatchRaysDimensions();

    Camera camera = Camera(uUniformBuffer.ViewInverse, uUniformBuffer.ProjInverse);

    RTPayload payload = RTPayload();
    payload.GBufferPass = false;
    payload.Sampler = Sampler(LaunchID.y + LaunchSize.x * LaunchID.x + getRealtimeClock().x);

    // Automatic Depth of field
    if (LaunchID.x == (LaunchSize.x / 2) && LaunchID.y == (LaunchSize.y / 2))
    {
        // If the current pixel is in the middle of the screen

        // Compute direction and shoot a ray into scene
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5, 0.5);
        const float2 inUV = pixelCenter / float2(LaunchSize.xy);
        Ray ray = camera.GetRay(0.0, 0.0, payload.Sampler, inUV * 2.0 - 1.0);

        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray.GetOrigin(), 0.0001, ray.GetDirection(), 1000000);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_NOTHING)
        {
            uDof[0] = query.CommittedRayT(); // Set DOF value to the nearest hit distance
        }
    }

    // Take Multiple samples per frame
    float3 finalValue = 0.0f;
    for (int i = 0; i < push.SamplesPerFrame; i++)
    {
        // Initialize payload for each sample
        float3 weight = 1.0f;
        float3 hitValue = 0.0f;

        payload.HitValue = float3(0.0f);
        payload.Weight = float3(1.0f);
        payload.Depth = 0;
        payload.InMedium = false;

        // Random float2 from -0.5 to 0.5
        const float2 antiAliasingJitter = (payload.Sampler.UniformFloat2() - 0.5f) * push.AAstrength;

        // Get ray direction
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5, 0.5) + antiAliasingJitter;
        const float2 inUV = pixelCenter / float2(LaunchSize.xy);
        Ray ray = camera.GetRay(push.FocalLenght, push.DoFStrenght, payload.Sampler, inUV * 2.0 - 1.0);

        // Start tracing
        for (; payload.Depth < push.MaxDepth; payload.Depth++)
        {
            // Check for volumes intersections which are done separately from the geometry because of rt pipeline limitations
            // e.g. inability to check whether you're inside the object or not
            float volumeScatter;
            float pathLengthThroughVolume;
            Volume volume;
            EnumerateThroughVolumes(ray, payload.Sampler, volumeScatter, pathLengthThroughVolume, volume);

            if (volumeScatter < FLT_MAX)
            {
                // Scattered inside the volume
                weight *= volume.Color.rgb;
                ray.SetOrigin(ray.GetOrigin() + ray.GetDirection() * volumeScatter);

                // MIS
                {
                    float3 dirToLight;
                    float4 envColor; // w = pdf
                    payload.Sampler.SampleImportanceEnvMap(uEnvironmentMap, uAccels, push.EnvAzimuth, push.EnvAltitude, dirToLight, envColor);
                
                    bool canHit = true;
                    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
                    query.__rayQueryInitializeEXT(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray.GetOrigin(), 0.0001, dirToLight, 1000000);
                    query.Proceed();
                    if (query.CommittedStatus() != COMMITTED_NOTHING)
                    {
                        canHit = false;
                    }
                
                    if (canHit)
                    {
                        float3 volumeAbsorption = float3(1.0f);
                        // Iterate through all volumes
                        for (int i = 0; i < push.VolumesCount; i++)
                        {
                            Volume volume1 = uVolumes[i];

                            // Check collision positions with volume
                            float3 hitPosNear;
                            float3 hitPosFar;
                            volume1.Aabb.IntersectRay(ray.GetOrigin(), dirToLight, hitPosNear, hitPosFar);

                            bool isInside = volume1.Aabb.IsInsideAABB(ray.GetOrigin());
                            if (isInside)
                                hitPosNear = ray.GetOrigin();

                            float volumeWidth = length(hitPosFar - hitPosNear);

                            volumeAbsorption *= exp(-volume1.ScatteringCoefficient * volumeWidth);
                        }

                        float phase = volume.EvaluateHenyeyGreenstein(payload.RayDirection, dirToLight);

                        hitValue += volumeAbsorption * weight * phase * (envColor.xyz / envColor.w) * exp(-volume.ScatteringCoefficient * pathLengthThroughVolume);
                    }
                }
                ray.SetDirection(payload.Sampler.SampleHenyeyGreenstein(ray.GetDirection(), volume.G));
            }
            else
            {
                // Scattered on surface
                ray.Trace(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, payload);

                if (payload.Depth == DEPTH_INFINITE && VectorEqual(payload.Weight, -1.0))
                    continue;

                weight *= payload.Weight;
                hitValue += payload.HitValue * weight;
            }

            // Russian Roulette

            float p = max(weight.x, max(weight.y, weight.z));

            // Because of energy compensation weight is sometimes greater than 1
            // and p gets compared to random variable that is between 0 and 1
            // it also has to be between 0 and 1 hence the min()
            p = min(p, 1.0f);
            if (p <= payload.Sampler.UniformFloat())
                break;
            weight /= p;
        }

        // Eliminate caustics
#ifdef USE_CAUSTICS_SUPPRESION
        const float maxLuminance = push.CausticsLuminance;
        float lum = dot(hitValue, float3(0.212671f, 0.715160f, 0.072169f));
        if (lum > maxLuminance)
        {
            hitValue *= maxLuminance / lum;
        }
#endif

        finalValue += hitValue;
    }
    finalValue /= push.SamplesPerFrame; // Average all samples

    film.WritePixel(LaunchID.xy, float4(finalValue, 1.0f), push.Frame);

    // If it's a first frame, shoot additional ray into the scene with GBufferPass flag set to true.
    // It is terminated when it hits the surface and it just retrieves color + normal of the surface
    // for the GBuffer that's later used by the denoiser
    if (push.Frame == 0)
    {
        const float2 pixelCenter = float2(LaunchID.xy) + float2(0.5);
        const float2 inUV = pixelCenter / float2(LaunchSize.xy);
        float2 d = inUV * 2.0 - 1.0;

        float4 origin = mul(float4(0, 0, 0, 1), uUniformBuffer.ViewInverse);
        float4 target = mul(float4(d.x, d.y, 1, 1), uUniformBuffer.ProjInverse);
        float3 direction = mul(float4(normalize(target.xyz), 0.0f), uUniformBuffer.ViewInverse).xyz;

        RayDesc rayDesc;
        rayDesc.TMin = 0.001;
        rayDesc.TMax = 10000.0;
        rayDesc.Origin = origin.xyz;
        rayDesc.Direction = direction;

        RTPayload payload1 = RTPayload();
        payload1.GBufferPass = true;
        payload1.RayOrigin = origin.xyz;
        payload1.RayDirection = direction;

        TraceRay(uTopLevelAS, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, rayDesc, payload1);

        uGBufferAlbedo[LaunchID.xy] = float4(payload1.GBufferColor, 1.0f);
        uGBufferNormal[LaunchID.xy] = float4((payload1.GBufferNormal + 1.0f) * 0.5f, 1.0f);
    }
}
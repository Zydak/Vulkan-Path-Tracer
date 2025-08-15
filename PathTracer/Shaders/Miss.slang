import RTCommon;

[shader("miss")]
void Main(inout Payload payload)
{
    float dir = payload.Direction.z;
    // Color gradient based on the z direction
    payload.LightColor = float4(0.5f + 0.5f * dir, 0.7f + 0.3f * dir, 1.0f - 0.5f * dir, 1.0f);
    payload.LightColor = float4(1.0f);
    payload.Depth = 0xFFFFFFFF; // Set depth to max value to indicate no hit
}
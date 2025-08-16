import RTCommon;

[shader("miss")]
void Main(inout Payload payload)
{
    float dir = payload.Direction.z;
    // Color gradient based on the z direction
    payload.LightColor += float4(1.0f);
    payload.Depth = 0xFFFFFFFF; // Set depth to max value to indicate no hit
}
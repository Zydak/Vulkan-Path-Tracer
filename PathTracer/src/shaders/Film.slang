module Film;

public struct Film
{
    RWTexture2D<float4> m_Image;

    public __init(RWTexture2D<float4> image)
    {
        m_Image = image;
    }

    public void WritePixel(uint2 location, float4 value, uint64_t frame)
    {
        if (!isinf(value.x) && !isinf(value.y) && !isinf(value.z) && !isnan(value.x) && !isnan(value.y) && !isnan(value.z))
        {
            if (frame > 0)
            {
                float a = 1.0f / float(frame + 1);
                float4 oldColor = m_Image.Load(location);
                m_Image[location] = lerp(oldColor, value, a);
            }
            else
            {
                m_Image[location] = value;
            }
        }
    }
}
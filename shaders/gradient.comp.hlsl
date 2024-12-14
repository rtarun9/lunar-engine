RWTexture2D<float4> texture : register(b0);

[numthreads(16,16,1)]
void cs_main(uint3 index :SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint texture_width = 0;
    uint texture_height = 0;

    texture.GetDimensions(texture_width, texture_height);

    if (index.x < texture_width && index.y < texture_height)
    {
        if (group_thread_id.x != 0 && group_thread_id.y != 0)
        {
            float x_color = index.x / (float)texture_width;
            float y_color = index.y / (float)texture_height;

            texture[index.xy] = float4(x_color, y_color, 0.0f, 1.0f);
        }
    }
}
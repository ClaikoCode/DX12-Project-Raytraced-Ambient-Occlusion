struct VSQuadOut
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

struct GlobalFrameData
{
    uint frameCount;
    uint accumulatedFrames;
    float time;
};

ConstantBuffer<GlobalFrameData> frameData : register(b1);

Texture2D<float4> currentFrame : register(t3);

RWTexture2D<float4> accumilationTexture : register(u0);

SamplerState textureSampler : register(s0);

uint2 GetPixelIndex(const in float2 uv)
{
    uint width, height;
    accumilationTexture.GetDimensions(width, height);
    int xPos = uv.x * width;
    int yPos = uv.y * height;
    uint2 pixelIndex = uint2(xPos, yPos);
    
    return pixelIndex;
}

float4 main(VSQuadOut input) : SV_TARGET0
{
    const uint accumulatedFrames = clamp(frameData.accumulatedFrames, 0, 35);
    
    uint2 pixelIndex = GetPixelIndex(input.texcoord);
    
    // Read from accumilation and save the original color.
    float4 prevColor = accumilationTexture[pixelIndex];
    // Read form the current frame that was just created.
    float4 currentColor = currentFrame.Sample(textureSampler, input.texcoord);
    
    // Save the final color as a weighted sum.
    float4 finalColor = (accumulatedFrames * prevColor + currentColor) / (accumulatedFrames + 1);
    
    // Read back to accumilation texture.
    accumilationTexture[pixelIndex] = finalColor;
    
    // Write the same color to the render target.
    return finalColor;
}
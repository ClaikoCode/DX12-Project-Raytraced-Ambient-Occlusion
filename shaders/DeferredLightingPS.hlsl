struct VSQuadOut
{
	float4 position : SV_Position;
	float2 texcoord : UV;
};

Texture2D<float4> diffuse : register(t0);
Texture2D<float4> normal : register(t1);
Texture2D<float4> position : register(t2);

SamplerState textureSampler : register(s0);

struct DirectionalLight
{
    float3 dir;
};

float4 main(VSQuadOut input) : SV_Target0
{
    DirectionalLight testLight;
    testLight.dir = normalize(float3(-1.0f, -1.0f, 0.0f));
    
    float4 diffColor = diffuse.Sample(textureSampler, input.texcoord);
    float4 worldNormal = normal.Sample(textureSampler, input.texcoord);
    float4 worldPosition = position.Sample(textureSampler, input.texcoord);
    
    float lightStrength = clamp(dot((float3) worldNormal, -testLight.dir), 0.0f, 1.0f);
    
    float3 finalColor = (float3)diffColor * lightStrength;
    
    return float4(finalColor, 1.0f);
}
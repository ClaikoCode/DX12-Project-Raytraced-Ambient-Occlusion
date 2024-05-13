struct VSOut
{
    float4 pos : SV_POSITION;
    float4 worldPos : WORLD_POS;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float4 worldNormal : WORLD_NORMAL;
};

struct PSOut
{
    float4 diffuse : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 position : SV_TARGET2;
};

struct DirectionalLight
{
    float3 dir;
};

PSOut main(VSOut input)
{
    PSOut OUT;

    DirectionalLight testLight;
    testLight.dir = float3(0.0f, -1.0f, -1.0f);

    float lightStrength = clamp(dot(input.worldNormal, -testLight.dir), 0.0f, 1.0f);

    OUT.diffuse = input.worldNormal * lightStrength;
    OUT.normal = input.worldNormal;
    OUT.position = input.worldPos;

    return OUT;
}
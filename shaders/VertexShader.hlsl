struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct Rotation
{
    matrix transform;
};

ConstantBuffer<Rotation> rot : register(b0);

VSOut main(VSIn input)
{
    VSOut output = (VSOut) 0;
    
    output.pos = mul(float4(input.pos, 1.0f), rot.transform);
    output.color = float4(input.normal, 1.0f);

    return output;
}
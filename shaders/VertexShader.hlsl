struct VSOut
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

struct VSIn
{
    float3 pos : POSITION;
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
    
    output.pos = mul(rot.transform, float4(input.pos, 1.0f));
    output.color = float4(input.color, 1.0f);

    return output;
}
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

VSOut main(VSIn input, uint uVertexID : SV_VertexID)
{
    VSOut output = (VSOut) 0;
    
    output.pos = float4(input.pos, 1.0f);
    output.color = float4(input.color, 1.0f);

    return output;
}
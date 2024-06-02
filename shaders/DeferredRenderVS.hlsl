struct VSOut
{
    float4 pos : SV_POSITION;
    float4 worldPos : WORLD_POS;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float4 worldNormal : WORLD_NORMAL;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct ModelTransform
{
    matrix transform;
};

struct GlobalFrameData
{
    uint frameCount;
    uint accumulatedFrames;
    float time;
};

struct CameraInfo
{
    matrix viewProjMatrix;
};

ConstantBuffer<CameraInfo> camInfo : register(b0);
ConstantBuffer<GlobalFrameData> frameData : register(b1);
ConstantBuffer<ModelTransform> transf : register(b2);

VSOut main(VSIn input)
{
    VSOut output = (VSOut) 0;
    
    // TODO: Remove the need to do transpose in shader.
    matrix transposedTransform = transpose(transf.transform);
    matrix mvpMatrix = mul(transposedTransform, transpose(camInfo.viewProjMatrix));

    float4 inputPosition = float4(input.pos, 1.0f);
    output.pos = mul(inputPosition, mvpMatrix);
    output.worldPos = mul(inputPosition, transposedTransform);
   
    float4 inputNormal = float4(normalize(input.normal), 0.0f);
    output.normal = mul(inputNormal, mvpMatrix);    
    float3 worldNormal = mul(inputNormal, transposedTransform).xyz;
    output.worldNormal = float4(normalize(worldNormal), 0.0f);
    
    output.color = float4(input.color, 1.0f);

    return output;
}
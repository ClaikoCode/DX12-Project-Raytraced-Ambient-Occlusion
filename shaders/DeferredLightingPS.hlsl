struct VSQuadOut
{
	float4 position : SV_Position;
	float2 texcoord : UV;
};

Texture2D<float4> diffuse : register(t0);
Texture2D<float4> normal : register(t1);
Texture2D<float4> position : register(t2);

float4 main(VSQuadOut input) : SV_Target0
{
	
	
	return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
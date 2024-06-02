#pragma once 

#include "DX12RenderPass.h"

class AccumilationRenderPass : public DX12RenderPass
{
public:
	AccumilationRenderPass(ComPtr<ID3D12Device5> device, ComPtr<ID3D12RootSignature> rootSig);

	void BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs) override final;

protected:
	void PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
	void PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex) override final;
};
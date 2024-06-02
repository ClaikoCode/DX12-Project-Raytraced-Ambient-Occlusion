#include "IndexedRenderPass.h"

void IndexedRenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	assert(pipelineArgs != nullptr);
	IndexedRenderPassArgs& args = *reinterpret_cast<IndexedRenderPassArgs*>(pipelineArgs);

	auto commandList = GetCommandList(context, frameIndex);
	SetCommonStates(args.commonArgs, m_pipelineState, commandList);

	// Set render target and depth stencil.
	commandList->OMSetRenderTargets(1, &args.RTV, TRUE, &args.commonArgs.depthStencilView);

	// Draw.
	for (const RenderPackage& renderPackage : renderPackages)
	{
		if (renderPackage.renderObject)
		{
			const RenderObject& renderObject = *renderPackage.renderObject;

			PerRenderObject(renderObject, pipelineArgs, context, frameIndex);

			const std::vector<DrawArgs>& drawArgs = renderObject.drawArgs;

			if (renderPackage.renderInstances)
			{
				const std::vector<RenderInstance>& renderInstances = *renderPackage.renderInstances;

				for (const RenderInstance& renderInstance : renderInstances)
				{
					PerRenderInstance(renderInstance, drawArgs, pipelineArgs, context, frameIndex);
				}
			}
		}
	}
}

void IndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	auto commandList = GetCommandList(context, frameIndex);

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
	commandList->IASetIndexBuffer(&renderObject.indexBufferView);
}

void IndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	IndexedRenderPassArgs& args = ToSpecificArgs<IndexedRenderPassArgs>(pipelineArgs);
	auto commandList = GetCommandList(context, frameIndex);

	SetInstanceCB(args.commonArgs, frameIndex, renderInstance, commandList);

	DrawInstanceIndexed(context, drawArgs, commandList);
}
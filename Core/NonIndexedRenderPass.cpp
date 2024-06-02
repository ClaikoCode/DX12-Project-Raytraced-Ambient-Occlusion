#include "NonIndexedRenderPass.h"

void NonIndexedRenderPass::BuildRenderPass(const std::vector<RenderPackage>& renderPackages, UINT context, UINT frameIndex, RenderPassArgs* pipelineArgs)
{
	assert(pipelineArgs != nullptr);
	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);

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

				for (UINT i = context; i < drawArgs.size(); i += NumContexts)
				{
					PerRenderInstance(renderInstances[i], drawArgs, pipelineArgs, context, frameIndex);
				}
			}
		}
	}
}

void NonIndexedRenderPass::PerRenderObject(const RenderObject& renderObject, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	auto commandList = GetCommandList(context, frameIndex);

	commandList->IASetPrimitiveTopology(renderObject.topology);
	commandList->IASetVertexBuffers(0, 1, &renderObject.vertexBufferView);
}

void NonIndexedRenderPass::PerRenderInstance(const RenderInstance& renderInstance, const std::vector<DrawArgs>& drawArgs, RenderPassArgs* pipelineArgs, UINT context, UINT frameIndex)
{
	NonIndexedRenderPassArgs& args = ToSpecificArgs<NonIndexedRenderPassArgs>(pipelineArgs);
	auto commandList = GetCommandList(context, frameIndex);

	SetInstanceCB(args.commonArgs, frameIndex, renderInstance, commandList);

	for (UINT i = context; i < drawArgs.size(); i += NumContexts)
	{
		const DrawArgs& drawArg = drawArgs[i];

		commandList->DrawInstanced(
			drawArg.vertexCount,
			1,
			drawArg.startVertex,
			drawArg.startInstance
		);
	}
}

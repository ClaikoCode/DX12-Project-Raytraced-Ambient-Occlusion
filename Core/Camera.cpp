#include "Camera.h"

namespace dx = DirectX;

Camera::Camera(float fov, float aspectRatio, float nearZ, float farZ) : 
	m_data(fov, aspectRatio, nearZ, farZ), 
	m_position(dx::XMVectorSet(0.0f, 0.0f, -10.0f, 1.0f)),
	m_target(dx::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f)),
	m_up(dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)) 
{
	UpdateViewMatrix();
	UpdateProjectionMatrix();
	UpdateViewProjectionMatrix();
}

void Camera::UpdateViewMatrix()
{
	m_data.viewMatrix = dx::XMMatrixLookAtLH(m_position, m_target, m_up);
}

void Camera::UpdateProjectionMatrix()
{
	m_data.projectionMatrix = dx::XMMatrixPerspectiveFovLH(m_data.fov, m_data.aspectRatio, m_data.nearZ, m_data.farZ);
}

void Camera::UpdateViewProjectionMatrix()
{
	m_data.viewProjectionMatrix = dx::XMMatrixMultiply(GetViewMatrix(), GetProjectionMatrix());
}

DirectX::XMMATRIX Camera::GetViewMatrix() const
{
	return m_data.viewMatrix;
}

DirectX::XMMATRIX Camera::GetProjectionMatrix() const
{
	return m_data.projectionMatrix;
}

DirectX::XMMATRIX Camera::GetViewProjectionMatrix() const
{
	return m_data.viewProjectionMatrix;
}

void Camera::SetPosAndDir(std::array<float, 3> pos, std::array<float, 3> dir)
{
	m_position = dx::XMVectorSet(pos[0], pos[1], pos[2], 1.0f);

	// Build normalized dir vector.
	const dx::XMVECTOR dirVec = dx::XMVector3Normalize(dx::XMVectorSet(dir[0], dir[1], dir[2], 0.0f));
	m_target = dx::XMVectorAdd(m_position, dirVec);

	UpdateViewMatrix();
	UpdateViewProjectionMatrix();
}

const CameraData& Camera::GetData() const
{
	return m_data;
}


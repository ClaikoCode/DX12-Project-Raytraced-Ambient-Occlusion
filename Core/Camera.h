#pragma once 

#include <array>

#include "DirectXIncludes.h"

struct CameraData
{
	CameraData() = default;

	CameraData(float fov, float aspectRatio, float nearZ, float farZ) :
		fov(fov),
		aspectRatio(aspectRatio),
		nearZ(nearZ),
		farZ(farZ),
		viewMatrix(DirectX::XMMatrixIdentity()),
		projectionMatrix(DirectX::XMMatrixIdentity()),
		viewProjectionMatrix(DirectX::XMMatrixIdentity()) {}





	float fov;
	float aspectRatio;
	float nearZ;
	float farZ;

	DirectX::XMMATRIX viewMatrix;
	DirectX::XMMATRIX projectionMatrix;
	DirectX::XMMATRIX viewProjectionMatrix;
};

class Camera
{
public:
	Camera() = default;
	Camera(float fov, float aspectRatio, float nearZ, float farZ);

	void UpdateViewMatrix();
	void UpdateProjectionMatrix();
	void UpdateViewProjectionMatrix();

	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetViewProjectionMatrix() const;

	void SetPosAndDir(std::array<float, 3> pos, std::array<float, 3> dir);
	void SetPosAndLookAt(std::array<float, 3> pos, std::array<float, 3> dir);
	void SetPosAndLookAt(DirectX::XMVECTOR pos, DirectX::XMVECTOR lookAt);

	const CameraData& GetData() const;

private:
	CameraData m_data;

	DirectX::XMVECTOR m_position;
	DirectX::XMVECTOR m_target;
	DirectX::XMVECTOR m_up;
};
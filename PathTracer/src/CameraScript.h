#pragma once
#include <VulkanHelper.h>
#include "Components.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/compatibility.hpp"

class CameraScript : public VulkanHelper::ScriptInterface
{
public:
	CameraScript() {}
	~CameraScript() {} 

	void OnCreate() override
	{
		auto cameraCp = &m_Entity.GetComponent<PerspectiveCameraComponent>();

		cameraCp->Camera.UpdateViewMatrix();

		m_StartingTranslation = cameraCp->Camera.Translation;
		m_StartingRotation = cameraCp->Camera.Rotation.GetAngles();
	}

	void OnDestroy() override
	{

	}

	void OnUpdate(double deltaTime) override
	{
		auto& cameraComponent = GetComponent<PerspectiveCameraComponent>();

		if (m_Reset)
		{
			m_ResetTimer += deltaTime;
			OnReset();
			return;
		}

		if (m_CameraLocked)
		{
			return;
		}

		glm::vec2 mousePosition = VulkanHelper::Input::GetMousePosition();
		if (VulkanHelper::Input::IsMousePressed(0))
		{
			// Translation
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_A))
			{
				cameraComponent.Camera.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetRightVec();
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_D))
			{
				cameraComponent.Camera.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetRightVec();
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_W))
			{
				cameraComponent.Camera.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetFrontVec();
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_S))
			{
				cameraComponent.Camera.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetFrontVec();
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_SPACE))
			{
				cameraComponent.Camera.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetUpVec();
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_LEFT_SHIFT))
			{
				cameraComponent.Camera.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.Camera.GetUpVec();
			}

			// Rotation
			// Roll
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_Q))
			{
				cameraComponent.Camera.AddRoll((float)deltaTime * m_RotationSpeed);
			}
			if (VulkanHelper::Input::IsKeyPressed(VL_KEY_E))
			{
				cameraComponent.Camera.AddRoll(-(float)deltaTime * m_RotationSpeed);
			}

			// Pitch
			if (m_LastMousePosition.y != mousePosition.y)
			{
				cameraComponent.Camera.AddPitch((m_LastMousePosition.y - mousePosition.y) * (m_RotationSpeed / 200.0f));
			}

			// Yaw
			if (m_LastMousePosition.x != mousePosition.x)
			{
				cameraComponent.Camera.AddYaw(-(m_LastMousePosition.x - mousePosition.x) * (m_RotationSpeed / 200.0f));
			}
		}
		m_LastMousePosition = mousePosition;

		cameraComponent.Camera.UpdateViewMatrix();
	}

	void Reset()
	{
		m_Reset = true;
		m_ResetTimer = 0.0f;

		auto& cameraCp = GetComponent<PerspectiveCameraComponent>();

		m_ResetStartTranslation = cameraCp.Camera.Translation;
		m_ResetStartRotation = cameraCp.Camera.Rotation.GetAngles();
	}

	float m_MovementSpeed = 5.0f;
	float m_RotationSpeed = 15.0f;
	bool m_CameraLocked = true;
	bool m_Reset = false;
	float m_ResetDuration = 1.0f;
private:
	double m_ResetTimer = 0.0;
	glm::vec2 m_LastMousePosition{ 0.0f };
	glm::vec3 m_StartingTranslation;
	glm::vec3 m_StartingRotation;

	glm::vec3 m_ResetStartTranslation;
	glm::vec3 m_ResetStartRotation;

	void OnReset()
	{
		auto& cameraCp = GetComponent<PerspectiveCameraComponent>();

		cameraCp.Camera.Translation = glm::lerp(m_ResetStartTranslation, m_StartingTranslation, glm::smoothstep(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3((float)m_ResetTimer / m_ResetDuration)));
		cameraCp.Camera.Rotation.SetAngles(glm::lerp(m_ResetStartRotation, m_StartingRotation, glm::smoothstep(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3((float)m_ResetTimer / m_ResetDuration))));

		cameraCp.Camera.UpdateViewMatrix();

		if (m_ResetTimer / m_ResetDuration >= 1.0f)
		{
			m_Reset = false;
		}
	}
};
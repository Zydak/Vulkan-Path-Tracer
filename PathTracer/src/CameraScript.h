#pragma once
#include <Vulture.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/compatibility.hpp"

class CameraScript : public Vulture::ScriptInterface
{
public:
	CameraScript() {}
	~CameraScript() {} 

	void OnCreate() override
	{
		auto cameraCp = &m_Entity.GetComponent<Vulture::CameraComponent>();
		cameraCp->Reset();

		cameraCp->SetZoom(1.0f);
		cameraCp->SetPerspectiveMatrix(45.0f, m_Entity.GetScene()->GetWindow()->GetAspectRatio(), 0.1f, 100.0f);

		cameraCp->Translation.z = -10.0f;
		cameraCp->UpdateViewMatrix();

		m_StartingTranslation = cameraCp->Translation;
		m_StartingRotation = cameraCp->Rotation.GetAngles();
	}

	void OnDestroy() override
	{

	}

	void OnUpdate(double deltaTime) override
	{
		auto& cameraComponent = GetComponent<Vulture::CameraComponent>();

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

		// Translation
		if (Vulture::Input::IsKeyPressed(VL_KEY_A))
		{
			cameraComponent.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.GetRightVec();
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_D))
		{
			cameraComponent.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.GetRightVec();
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_W))
		{
			cameraComponent.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.GetFrontVec();
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_S))
		{
			cameraComponent.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.GetFrontVec();
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_SPACE))
		{
			cameraComponent.Translation += (float)deltaTime * m_MovementSpeed * cameraComponent.GetUpVec();
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_LEFT_SHIFT))
		{
			cameraComponent.Translation -= (float)deltaTime * m_MovementSpeed * cameraComponent.GetUpVec();
		}

		// Rotation
		// Roll
		if (Vulture::Input::IsKeyPressed(VL_KEY_Q))
		{
			cameraComponent.AddRoll((float)deltaTime * m_RotationSpeed);
		}
		if (Vulture::Input::IsKeyPressed(VL_KEY_E))
		{
			cameraComponent.AddRoll(-(float)deltaTime * m_RotationSpeed);
		}

		glm::vec2 mousePosition = Vulture::Input::GetMousePosition();
		if (Vulture::Input::IsMousePressed(0))
		{
			// Pitch
			if (m_LastMousePosition.y != mousePosition.y)
			{
				cameraComponent.AddPitch((m_LastMousePosition.y - mousePosition.y) * (m_RotationSpeed / 200.0f));
			}

			// Yaw
			if (m_LastMousePosition.x != mousePosition.x)
			{
				cameraComponent.AddYaw(-(m_LastMousePosition.x - mousePosition.x) * (m_RotationSpeed / 200.0f));
			}
		}
		m_LastMousePosition = mousePosition;

		cameraComponent.UpdateViewMatrix();
	}

	void Reset()
	{
		m_Reset = true;
		m_ResetTimer = 0.0f;

		auto& cameraCp = GetComponent<Vulture::CameraComponent>();

		m_ResetStartTranslation = cameraCp.Translation;
		m_ResetStartRotation = cameraCp.Rotation.GetAngles();
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
		auto& cameraCp = GetComponent<Vulture::CameraComponent>();

		cameraCp.Translation = glm::lerp(m_ResetStartTranslation, m_StartingTranslation, glm::smoothstep(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3((float)m_ResetTimer / m_ResetDuration)));
		cameraCp.Rotation.SetAngles(glm::lerp(m_ResetStartRotation, m_StartingRotation, glm::smoothstep(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3((float)m_ResetTimer / m_ResetDuration))));

		cameraCp.UpdateViewMatrix();

		if (m_ResetTimer / m_ResetDuration >= 1.0f)
		{
			m_Reset = false;
		}
	}
};
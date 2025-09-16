#pragma once

#include "VulkanHelper.h"

class FlyCamera
{
public:
    FlyCamera() = default;
    FlyCamera(const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix);

    // Movement functions
    void ProcessMouseMovement(float deltaX, float deltaY, bool constrainPitch = true);

    enum class Direction
    {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };

    void ProcessKeyboard(Direction direction, float deltaTime);

    // Setters
    void SetPosition(const glm::vec3& position);
    void SetRotation(float yaw, float pitch);
    void SetFov(float fov);
    void SetAspectRatio(float aspectRatio);
    void SetNearFar(float nearPlane, float farPlane);
    void SetMovementSpeed(float speed) { m_MovementSpeed = speed; }
    void SetMouseSensitivity(float sensitivity) { m_MouseSensitivity = sensitivity; }
    
    // Getters
    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::mat4 GetProjectionMatrix() const;
    [[nodiscard]] const glm::vec3& GetPosition() const { return m_Position; }
    [[nodiscard]] const glm::vec3& GetFront() const { return m_Front; }
    [[nodiscard]] const glm::vec3& GetUp() const { return m_Up; }
    [[nodiscard]] const glm::vec3& GetRight() const { return m_Right; }
    [[nodiscard]] float GetYaw() const { return m_Yaw; }
    [[nodiscard]] float GetPitch() const { return m_Pitch; }
    [[nodiscard]] float GetFov() const { return m_Fov; }
    [[nodiscard]] float GetAspectRatio() const { return m_AspectRatio; }
    [[nodiscard]] float GetMovementSpeed() const { return m_MovementSpeed; }
    [[nodiscard]] float GetMouseSensitivity() const { return m_MouseSensitivity; }

private:
    void UpdateCameraVectors();
    void InitializeFromMatrices(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
    // Camera attributes
    glm::vec3 m_Position = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 m_Front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 m_Up = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 m_Right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 m_WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Euler angles
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;
    
    // Camera options
    float m_MovementSpeed = 5.0f;
    float m_MouseSensitivity = 0.2f;
    float m_Fov = 45.0f;
    float m_AspectRatio = 16.0f / 9.0f;
    float m_NearPlane = 0.1f;
    float m_FarPlane = 1000.0f;
};

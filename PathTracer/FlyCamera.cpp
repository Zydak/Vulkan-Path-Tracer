#include "FlyCamera.h"

FlyCamera::FlyCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    InitializeFromMatrices(viewMatrix, projectionMatrix);
}

void FlyCamera::ProcessMouseMovement(float deltaX, float deltaY, bool constrainPitch)
{
    deltaX *= m_MouseSensitivity;
    deltaY *= m_MouseSensitivity;

    m_Yaw += deltaX;
    m_Pitch += deltaY;

    // Make sure that when pitch is out of bounds, screen doesn't get flipped
    if (constrainPitch)
    {
        if (m_Pitch > 89.0f)
            m_Pitch = 89.0f;
        if (m_Pitch < -89.0f)
            m_Pitch = -89.0f;
    }

    // Update Front, Right and Up Vectors using the updated Euler angles
    UpdateCameraVectors();
}

void FlyCamera::ProcessKeyboard(Direction direction, float deltaTime)
{
    float velocity = m_MovementSpeed * deltaTime;
    
    switch (direction)
    {
        case Direction::FORWARD:
            m_Position += m_Front * velocity;
            break;
        case Direction::BACKWARD:
            m_Position -= m_Front * velocity;
            break;
        case Direction::LEFT:
            m_Position -= m_Right * velocity;
            break;
        case Direction::RIGHT:
            m_Position += m_Right * velocity;
            break;
        case Direction::UP:
            m_Position -= m_WorldUp * velocity;
            break;
        case Direction::DOWN:
            m_Position += m_WorldUp * velocity;
            break;
    }
}

void FlyCamera::SetPosition(const glm::vec3& position)
{
    m_Position = position;
}

void FlyCamera::SetRotation(float yaw, float pitch)
{
    m_Yaw = yaw;
    m_Pitch = pitch;
    UpdateCameraVectors();
}

void FlyCamera::SetFov(float fov)
{
    m_Fov = fov;
}

void FlyCamera::SetAspectRatio(float aspectRatio)
{
    m_AspectRatio = aspectRatio;
}

void FlyCamera::SetNearFar(float nearPlane, float farPlane)
{
    m_NearPlane = nearPlane;
    m_FarPlane = farPlane;
}

glm::mat4 FlyCamera::GetViewMatrix() const
{
    glm::mat4 view = glm::lookAt(m_Position, m_Position + m_Front, m_Up);

    glm::mat4 vulkanTransform = glm::mat4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    return view * vulkanTransform;
}

glm::mat4 FlyCamera::GetProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearPlane, m_FarPlane);
}

void FlyCamera::UpdateCameraVectors()
{
    // Calculate the new Front vector
    glm::vec3 front;
    front.x = cosf(glm::radians(m_Yaw)) * cosf(glm::radians(m_Pitch));
    front.y = sinf(glm::radians(m_Pitch));
    front.z = sinf(glm::radians(m_Yaw)) * cosf(glm::radians(m_Pitch));
    m_Front = glm::normalize(front);

    // Also recalculate the Right and Up vectors
    m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
    m_Up = glm::normalize(glm::cross(m_Right, m_Front));
}

void FlyCamera::InitializeFromMatrices(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    glm::mat3 rotation = glm::mat3(viewMatrix);
    glm::vec3 translation = glm::vec3(viewMatrix[3]);
    m_Position = -glm::transpose(rotation) * translation;
    
    glm::vec3 forward = -glm::normalize(glm::vec3(viewMatrix[2]));
    
    m_Yaw = glm::degrees(atan2f(forward.z, -forward.x));
    m_Pitch = glm::degrees(asinf(forward.y));
    m_Fov = glm::degrees(2.0f * atanf(1.0f / projectionMatrix[1][1]));
    m_AspectRatio = projectionMatrix[1][1] / projectionMatrix[0][0];
    
    UpdateCameraVectors();
}
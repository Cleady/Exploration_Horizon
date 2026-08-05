#include "Camera.hpp"

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Fov(FOV) {
    Position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    updateCameraVectors();
}

Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Fov(FOV) {
    Position = glm::vec3(posX, posY, posZ);
    WorldUp = glm::vec3(upX, upY, upZ);
    Yaw = yaw;
    Pitch = pitch;
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(Fov), aspectRatio, 0.1f, 500.0f);
}

void Camera::processKeyboard(Camera_Movement direction, float deltaTime) {
    float speed = MovementSpeed * (m_sprinting ? SPRINT_MULTIPLIER : 1.0f);
    float velocity = speed * deltaTime;
    if (direction == FORWARD)  Position += Front    * velocity;
    if (direction == BACKWARD) Position -= Front    * velocity;
    if (direction == LEFT)     Position -= Right    * velocity;
    if (direction == RIGHT)    Position += Right    * velocity;
    if (direction == UP)       Position += WorldUp  * velocity;
    if (direction == DOWN)     Position -= WorldUp  * velocity;
}

glm::vec3 Camera::getMoveDelta(Camera_Movement direction, float deltaTime) const {
    float speed    = MovementSpeed * (m_sprinting ? SPRINT_MULTIPLIER : 1.0f);
    float velocity = speed * deltaTime;
    if (direction == FORWARD)  return  Front   * velocity;
    if (direction == BACKWARD) return -Front   * velocity;
    if (direction == LEFT)     return -Right   * velocity;
    if (direction == RIGHT)    return  Right   * velocity;
    if (direction == UP)       return  WorldUp * velocity;
    if (direction == DOWN)     return -WorldUp * velocity;
    return glm::vec3(0.0f);
}

glm::vec3 Camera::getWalkMoveDelta(Camera_Movement direction, float deltaTime) const {
    float speed    = MovementSpeed * (m_sprinting ? SPRINT_MULTIPLIER : 1.0f);
    float velocity = speed * deltaTime;

    glm::vec3 frontXZ(Front.x, 0.0f, Front.z);
    if (glm::length(frontXZ) > 0.0001f) frontXZ = glm::normalize(frontXZ);

    glm::vec3 rightXZ(Right.x, 0.0f, Right.z);
    if (glm::length(rightXZ) > 0.0001f) rightXZ = glm::normalize(rightXZ);

    if (direction == FORWARD)  return  frontXZ * velocity;
    if (direction == BACKWARD) return -frontXZ * velocity;
    if (direction == LEFT)     return -rightXZ * velocity;
    if (direction == RIGHT)    return  rightXZ * velocity;
    return glm::vec3(0.0f);
}

void Camera::processMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch) {
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw   += xoffset;
    Pitch += yoffset;

    if (constrainPitch) {
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);
    
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up    = glm::normalize(glm::cross(Right, Front));
}

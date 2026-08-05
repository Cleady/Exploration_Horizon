#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

// Default camera constants
const float YAW                = -90.0f;
const float PITCH              =  0.0f;
const float SPEED              =  4.5f;
const float SPRINT_MULTIPLIER  =  2.0f;   // fast-fly while holding LEFT CTRL (exactly 2x)
const float SENSITIVITY        =  0.1f;
const float FOV                =  45.0f;

class Camera {
public:
    // Camera Attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    // Euler Angles
    float Yaw;
    float Pitch;

    // Camera options
    float MovementSpeed;
    float MouseSensitivity;
    float Fov;

    // Physics & Movement States
    bool isFlying = true;
    bool isGrounded = false;
    float verticalVelocity = 0.0f;
    static constexpr float GRAVITY       = -28.0f;
    static constexpr float JUMP_FORCE     =  8.5f;
    static constexpr float PLAYER_HEIGHT =  1.8f;

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);
    Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    void processKeyboard(Camera_Movement direction, float deltaTime);

    // Returns the intended movement delta for `direction` without applying it.
    // Use this when the caller needs to run AABB collision resolution before
    // committing the new position (e.g. main.cpp's resolveCollision pass).
    glm::vec3 getMoveDelta(Camera_Movement direction, float deltaTime) const;

    // Returns horizontal-only movement delta (constrained to XZ plane for walking).
    glm::vec3 getWalkMoveDelta(Camera_Movement direction, float deltaTime) const;

    void processMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true);

    // Sprint / fast-fly mode: multiplies movement speed while enabled.
    void setSprint(bool sprint) { m_sprinting = sprint; }
    bool isSprinting() const { return m_sprinting; }

private:
    bool m_sprinting = false;
    void updateCameraVectors();
};

#endif // CAMERA_HPP

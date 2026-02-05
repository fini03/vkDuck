// vim:foldmethod=marker
#pragma once

#include <vkDuck/vulkan_base.h>

// Camera controller types
enum class CameraType { Fixed, FPS, Orbital };

// GPU-ready camera data structure {{{
struct CameraData {
    alignas(16) glm::mat4 view{1.0f};
    alignas(16) glm::mat4 invView{1.0f};
    alignas(16) glm::mat4 proj{1.0f};
};
// }}}

// Camera controller class for FPS and Orbital cameras {{{
class CameraController {
public:
    CameraController() = default;
    ~CameraController() = default;

    // Camera type
    CameraType type{CameraType::Fixed};

    // Camera position/orientation
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    // FPS/Orbital parameters
    float yaw{0.0f};
    float pitch{0.0f};
    float distance{5.0f}; // Orbital only

    // Control speeds
    float moveSpeed{5.0f};
    float rotateSpeed{0.005f};
    float zoomSpeed{0.5f};

    // Projection parameters
    float fov{45.0f};
    float nearPlane{0.1f};
    float farPlane{1000.0f};
    float aspectRatio{16.0f / 9.0f};

    // Mouse state
    bool mouseGrabbed{false};
    bool firstMouse{true};
    float lastX{0.0f};
    float lastY{0.0f};

    // Initialize camera with specific type and parameters
    void init(CameraType cameraType,
              const glm::vec3& pos,
              const glm::vec3& tgt,
              const glm::vec3& upDir,
              float yawAngle, float pitchAngle, float dist,
              float movSpd, float rotSpd, float zoomSpd,
              float fieldOfView, float near, float far);

    // Update camera matrices and return the view matrix
    glm::mat4 getViewMatrix() const;

    // Update projection matrix
    glm::mat4 getProjectionMatrix() const;

    // Input handling
    void processKeyboard(float deltaTime, bool forward, bool backward,
                        bool left, bool right, bool up, bool down);
    void processMouseDrag(float deltaX, float deltaY);
    void processScroll(float delta);

    // Mouse grab handling
    void setMouseGrabbed(bool grabbed);
    void processMouseMotion(float x, float y);

    // Check if this is a movable camera
    bool isMovable() const { return type != CameraType::Fixed; }

    // Set aspect ratio (should be called when window/swapchain is resized)
    void setAspectRatio(float ratio) { aspectRatio = ratio; }

    // Get GPU-ready camera data (view, invView, proj matrices)
    CameraData getCameraData() const;

private:
    // Helper for orbital camera
    void updatePositionFromOrbit();

    // Helper for FPS camera
    void updateTargetFromOrientation();
};
// }}}

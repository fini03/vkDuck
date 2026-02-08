// vim:foldmethod=marker
#include <vkDuck/camera_controller.h>
#include <algorithm>

// CameraController implementation {{{

void CameraController::init(CameraType cameraType,
                            const glm::vec3& pos,
                            const glm::vec3& tgt,
                            const glm::vec3& upDir,
                            float yawAngle, float pitchAngle, float dist,
                            float movSpd, float rotSpd, float zoomSpd,
                            float fieldOfView, float nearP, float farP) {

    type = cameraType;
    position = pos;
    target = tgt;
    up = upDir;
    yaw = yawAngle;
    pitch = pitchAngle;
    distance = dist;
    moveSpeed = movSpd;
    rotateSpeed = rotSpd;
    zoomSpeed = zoomSpd;
    fov = fieldOfView;
    nearPlane = nearP;
    farPlane = farP;

    // Ensure position/target consistency based on camera type
    if (type == CameraType::Orbital) {
        updatePositionFromOrbit();
    } else if (type == CameraType::FPS) {
        updateTargetFromOrientation();
    }
}

glm::mat4 CameraController::getViewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 CameraController::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    proj[1][1] *= -1; // Flip Y for Vulkan
    return proj;
}

void CameraController::processKeyboard(float deltaTime, bool forward, bool backward,
                                       bool left, bool right, bool upKey, bool downKey) {
    if (type == CameraType::Fixed) return;

    // Calculate front direction with safety check
    glm::vec3 diff = target - position;
    float length = glm::length(diff);
    if (length < 0.0001f) {
        // Default front direction if target == position
        diff = glm::vec3(0.0f, 0.0f, -1.0f);
        length = 1.0f;
    }
    glm::vec3 front = diff / length;
    glm::vec3 rightDir = glm::normalize(glm::cross(front, up));
    float velocity = moveSpeed * deltaTime;

    if (type == CameraType::FPS) {
        // FPS: Move camera position directly
        if (forward) {
            position += front * velocity;
            target += front * velocity;
        }
        if (backward) {
            position -= front * velocity;
            target -= front * velocity;
        }
        if (left) {
            position -= rightDir * velocity;
            target -= rightDir * velocity;
        }
        if (right) {
            position += rightDir * velocity;
            target += rightDir * velocity;
        }
        if (upKey) {
            position += up * velocity;
            target += up * velocity;
        }
        if (downKey) {
            position -= up * velocity;
            target -= up * velocity;
        }
    } else if (type == CameraType::Orbital) {
        // Orbital: Move target, camera follows
        if (forward) target += front * velocity;
        if (backward) target -= front * velocity;
        if (left) target -= rightDir * velocity;
        if (right) target += rightDir * velocity;
        if (upKey) target += up * velocity;
        if (downKey) target -= up * velocity;
        updatePositionFromOrbit();
    }
}

void CameraController::processMouseDrag(float deltaX, float deltaY) {
    if (type == CameraType::Fixed) return;

    yaw -= deltaX * rotateSpeed;
    pitch -= deltaY * rotateSpeed;

    // Clamp pitch to avoid flipping
    const float maxPitch = glm::radians(89.0f);
    pitch = std::clamp(pitch, -maxPitch, maxPitch);

    if (type == CameraType::FPS) {
        updateTargetFromOrientation();
    } else if (type == CameraType::Orbital) {
        updatePositionFromOrbit();
    }
}

void CameraController::processScroll(float delta) {
    if (type != CameraType::Orbital) return;

    distance -= delta * zoomSpeed;
    distance = std::clamp(distance, 0.5f, 100.0f);
    updatePositionFromOrbit();
}

void CameraController::setMouseGrabbed(bool grabbed) {
    mouseGrabbed = grabbed;
    if (grabbed) {
        firstMouse = true;
    }
}

void CameraController::processMouseMotion(float x, float y) {
    if (!mouseGrabbed) return;

    if (firstMouse) {
        lastX = x;
        lastY = y;
        firstMouse = false;
        return;
    }

    float deltaX = x - lastX;
    float deltaY = y - lastY;
    lastX = x;
    lastY = y;

    processMouseDrag(deltaX, deltaY);
}

void CameraController::updatePositionFromOrbit() {
    position.x = target.x + distance * cos(pitch) * sin(yaw);
    position.y = target.y + distance * sin(pitch);
    position.z = target.z + distance * cos(pitch) * cos(yaw);
}

void CameraController::updateTargetFromOrientation() {
    glm::vec3 front;
    front.x = cos(pitch) * sin(yaw);
    front.y = sin(pitch);
    front.z = cos(pitch) * cos(yaw);
    front = glm::normalize(front);
    target = position + front * 5.0f;
}

CameraData CameraController::getCameraData() const {
    CameraData data;
    data.view = getViewMatrix();
    data.invView = glm::inverse(data.view);
    data.proj = getProjectionMatrix();
    return data;
}

// }}}

#pragma once
#include "node.h"
#include "vulkan_editor/io/serialization.h"
#include "vulkan_editor/gpu/primitives.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Use shared camera controller from vkDuck library
#include <vkDuck/camera_controller.h>

using namespace ShaderTypes;

struct GLTFCamera;

/**
 * @class CameraNodeBase
 * @brief Abstract base class for camera nodes providing view/projection matrices.
 *
 * Uses the shared CameraController from vkDuck library for camera math and input
 * processing. Derived classes configure the controller for specific behaviors
 * (orbital, FPS, fixed).
 */
class CameraNodeBase : public Node, public ISerializable {
public:
    CameraNodeBase();
    CameraNodeBase(int id);
    virtual ~CameraNodeBase();

    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const override;

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Camera controller from vkDuck library (handles all camera math)
    CameraController controller;

    // Camera data for GPU (updated from controller)
    CameraData cameraData{};

    // Editor camera type (for primitive creation)
    primitives::CameraType cameraType{primitives::CameraType::Fixed};

    // Update matrices from controller state
    virtual void updateMatrices();

    // Input processing - delegates to controller
    virtual void processKeyboard(
        float deltaTime, bool forward, bool backward,
        bool left, bool right, bool upKey, bool downKey
    );
    virtual void processMouseDrag(float deltaX, float deltaY);
    virtual void processScroll(float delta);

    virtual void saveInitialState();
    virtual void resetToInitialState();
    bool hasInitialState() const { return initialStateSaved; }
    virtual primitives::CameraType getCameraType() const {
        return primitives::CameraType::Fixed;
    }

    // Convenience accessors (delegate to controller)
    float getFov() const { return controller.fov; }
    void setFov(float f) { controller.fov = f; }
    float getNearPlane() const { return controller.nearPlane; }
    void setNearPlane(float n) { controller.nearPlane = n; }
    float getFarPlane() const { return controller.farPlane; }
    void setFarPlane(float f) { controller.farPlane = f; }
    float getAspectRatio() const { return controller.aspectRatio; }
    void setAspectRatio(float a) { controller.aspectRatio = a; }
    glm::vec3 getPosition() const { return controller.position; }
    void setPosition(const glm::vec3& p) { controller.position = p; }
    glm::vec3 getTarget() const { return controller.target; }
    void setTarget(const glm::vec3& t) { controller.target = t; }
    glm::vec3 getUp() const { return controller.up; }
    void setUp(const glm::vec3& u) { controller.up = u; }

    Pin cameraPin;

protected:
    // Initial state for reset functionality
    glm::vec3 initialPosition{0.0f, 0.0f, 5.0f};
    glm::vec3 initialTarget{0.0f, 0.0f, 0.0f};
    glm::vec3 initialUp{0.0f, 1.0f, 0.0f};
    bool initialStateSaved{false};

    void createDefaultPins();
    void renderCameraNode(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        ImColor headerColor,
        const NodeGraph& graph
    ) const;

    primitives::UniformBuffer* cameraUbo{nullptr};
    primitives::Camera* cameraPrimitive{nullptr};
    primitives::StoreHandle cameraUboArray{};
};

/**
 * @class OrbitalCameraNode
 * @brief Camera that orbits around a target point with mouse/keyboard controls.
 */
class OrbitalCameraNode : public CameraNodeBase {
public:
    OrbitalCameraNode();
    OrbitalCameraNode(int id);
    ~OrbitalCameraNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;
    void createPrimitives(primitives::Store& store) override;

    // Convenience accessors for orbital-specific params
    float getDistance() const { return controller.distance; }
    void setDistance(float d) { controller.distance = d; }
    float getYaw() const { return controller.yaw; }
    void setYaw(float y) { controller.yaw = y; }
    float getPitch() const { return controller.pitch; }
    void setPitch(float p) { controller.pitch = p; }
    float getMoveSpeed() const { return controller.moveSpeed; }
    void setMoveSpeed(float s) { controller.moveSpeed = s; }
    float getRotateSpeed() const { return controller.rotateSpeed; }
    void setRotateSpeed(float s) { controller.rotateSpeed = s; }
    float getZoomSpeed() const { return controller.zoomSpeed; }
    void setZoomSpeed(float s) { controller.zoomSpeed = s; }

    void applyGLTFCamera(const GLTFCamera& gltfCamera);
    void saveInitialState() override;
    void resetToInitialState() override;
    primitives::CameraType getCameraType() const override {
        return primitives::CameraType::Orbital;
    }

private:
    float initialDistance{5.0f};
    float initialYaw{0.0f};
    float initialPitch{0.0f};
};

using CameraNode = OrbitalCameraNode;

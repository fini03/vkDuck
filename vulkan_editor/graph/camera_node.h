#pragma once
#include "node.h"
#include "vulkan_editor/io/serialization.h"
#include "vulkan_editor/gpu/primitives.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace ShaderTypes;

struct GLTFCamera;

/**
 * @class CameraNodeBase
 * @brief Abstract base class for camera nodes providing view/projection matrices.
 *
 * Manages projection parameters (FOV, near/far planes) and computes view/projection
 * matrices for shader binding. Derived classes implement specific camera behaviors
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

    float fov{45.0f};
    float nearPlane{0.1f};
    float farPlane{1000.0f};
    float aspectRatio{16.0f / 9.0f};

    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    primitives::CameraData cameraData{};
    primitives::CameraType cameraType{primitives::CameraType::Fixed};

    virtual void updateMatrices();
    virtual void processKeyboard(
        float deltaTime, bool forward, bool backward,
        bool left, bool right, bool up, bool down
    ) {}
    virtual void processMouseDrag(float deltaX, float deltaY) {}
    virtual void processScroll(float delta) {}

    virtual void saveInitialState();
    virtual void resetToInitialState();
    bool hasInitialState() const { return initialStateSaved; }
    virtual primitives::CameraType getCameraType() const {
        return primitives::CameraType::Fixed;
    }

    Pin cameraPin;

protected:
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

    float distance{5.0f};
    float yaw{0.0f};
    float pitch{0.0f};
    float moveSpeed{5.0f};
    float rotateSpeed{0.005f};
    float zoomSpeed{0.5f};

    void processKeyboard(
        float deltaTime, bool forward, bool backward,
        bool left, bool right, bool up, bool down
    ) override;
    void processMouseDrag(float deltaX, float deltaY) override;
    void processScroll(float delta) override;
    void updatePositionFromOrbit();
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

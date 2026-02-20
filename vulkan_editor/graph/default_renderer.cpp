#include "default_renderer.h"
#include "fixed_camera_node.h"
#include "light_node.h"
#include "model_node.h"
#include "pipeline_node.h"
#include "present_node.h"
#include "../util/logger.h"
#include "../shader/shader_manager.h"

namespace fs = std::filesystem;

bool DefaultRendererSetup::createForModel(
    NodeGraph& graph,
    ModelNode* modelNode,
    ShaderManager& shaderManager,
    const fs::path& projectRoot
) {
    if (!modelNode) {
        Log::error("DefaultRenderer", "Cannot create renderer: modelNode is null");
        return false;
    }

    // Check if default shaders exist
    fs::path vertShaderPath = projectRoot / SHADER_DIR / DEFAULT_VERT_SHADER;
    fs::path fragShaderPath = projectRoot / SHADER_DIR / DEFAULT_FRAG_SHADER;

    if (!fs::exists(vertShaderPath)) {
        Log::error("DefaultRenderer", "Default vertex shader not found: {}", vertShaderPath.string());
        return false;
    }

    if (!fs::exists(fragShaderPath)) {
        Log::error("DefaultRenderer", "Default fragment shader not found: {}", fragShaderPath.string());
        return false;
    }

    Log::info("DefaultRenderer", "Creating default renderer for model: {}", modelNode->name);

    // Get model node position for layout
    ImVec2 modelPos = modelNode->position;

    // Camera setup: use GLTF camera if available, otherwise create a fixed camera
    bool useGLTFCamera = !modelNode->gltfCameras.empty();
    FixedCameraNode* fixedCamPtr = nullptr;
    ax::NodeEditor::PinId cameraPinToConnect;

    if (useGLTFCamera) {
        // Use the first GLTF camera from the model
        modelNode->selectedCameraIndex = 0;
        modelNode->updateCameraFromSelection();
        cameraPinToConnect = modelNode->cameraPin.id;
        Log::info("DefaultRenderer", "Using GLTF camera '{}' from model",
                  modelNode->gltfCameras[0].name);
    } else {
        // Create a Fixed Camera Node positioned to look at the model
        auto cameraNode = std::make_unique<FixedCameraNode>();
        cameraNode->name = "Default Camera";
        // Set the node graph UI position
        static_cast<Node*>(cameraNode.get())->position = ImVec2(modelPos.x - 300, modelPos.y - 100);
        // Set camera world position (looking at origin from a distance)
        cameraNode->setPosition(glm::vec3(0.0f, 2.0f, 5.0f));
        cameraNode->setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
        cameraNode->setUp(glm::vec3(0.0f, 1.0f, 0.0f));
        cameraNode->updateMatrices();
        fixedCamPtr = cameraNode.get();
        cameraPinToConnect = fixedCamPtr->cameraPin.id;
        graph.addNode(std::move(cameraNode));
        Log::info("DefaultRenderer", "Created fixed camera (no GLTF camera in model)");
    }

    // Create Light Node
    auto lightNode = std::make_unique<LightNode>();
    lightNode->name = "Default Light";
    lightNode->position = ImVec2(modelPos.x - 300, modelPos.y + 100);
    lightNode->numLights = 1;
    lightNode->ensureLightCount();
    // Position light above and to the side
    lightNode->lights[0].position = glm::vec3(5.0f, 5.0f, 5.0f);
    lightNode->lights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
    lightNode->lights[0].radius = 20.0f;
    LightNode* lightPtr = lightNode.get();
    graph.addNode(std::move(lightNode));

    // Create Pipeline Node with default Phong shaders
    auto pipelineNode = std::make_unique<PipelineNode>();
    pipelineNode->name = "Default Phong";
    pipelineNode->position = ImVec2(modelPos.x + 300, modelPos.y);
    pipelineNode->isMainPipeline = true;

    // Set shader paths (project-relative)
    pipelineNode->settings.vertexShaderPath = fs::path(SHADER_DIR) / DEFAULT_VERT_SHADER;
    pipelineNode->settings.fragmentShaderPath = fs::path(SHADER_DIR) / DEFAULT_FRAG_SHADER;

    // Default pipeline settings for basic rendering
    pipelineNode->settings.depthTest = true;
    pipelineNode->settings.depthWrite = true;
    pipelineNode->settings.cullMode = 1;  // Back face culling
    pipelineNode->settings.frontFace = 1; // Counter-clockwise

    PipelineNode* pipelinePtr = pipelineNode.get();
    graph.addNode(std::move(pipelineNode));

    // Trigger shader reflection to create pins
    bool shaderSuccess = shaderManager.reflectShader(pipelinePtr, graph);
    if (!shaderSuccess) {
        Log::error("DefaultRenderer", "Failed to reflect default shaders");
        return false;
    }

    // Create Present Node
    auto presentNode = std::make_unique<PresentNode>();
    presentNode->name = "Screen";
    presentNode->position = ImVec2(modelPos.x + 600, modelPos.y);
    PresentNode* presentPtr = presentNode.get();
    graph.addNode(std::move(presentNode));

    // Now create links between nodes
    // We need to find the correct pins on the pipeline node

    // Helper lambda to find input pin by label
    auto findInputPin = [&](PipelineNode* pipe, const std::string& label) -> ax::NodeEditor::PinId {
        for (const auto& binding : pipe->inputBindings) {
            if (binding.pin.label == label) {
                return binding.pin.id;
            }
        }
        // Check camera input
        if (pipe->hasCameraInput && pipe->cameraInput.pin.label == label) {
            return pipe->cameraInput.pin.id;
        }
        // Check light input
        if (pipe->hasLightInput && pipe->lightInput.pin.label == label) {
            return pipe->lightInput.pin.id;
        }
        return ax::NodeEditor::PinId();
    };

    // Helper lambda to find output attachment pin
    auto findOutputPin = [&](PipelineNode* pipe, const std::string& label) -> ax::NodeEditor::PinId {
        for (const auto& config : pipe->shaderReflection.attachmentConfigs) {
            if (config.pin.label == label || config.name == label) {
                return config.pin.id;
            }
        }
        return ax::NodeEditor::PinId();
    };

    // Link model vertex data to pipeline
    if (pipelinePtr->vertexDataPin.id.Get() != 0) {
        Link vertexLink;
        vertexLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        vertexLink.startPin = modelNode->vertexDataPin.id;
        vertexLink.endPin = pipelinePtr->vertexDataPin.id;
        graph.addLink(vertexLink);
        Log::debug("DefaultRenderer", "Linked: Model vertex data -> Pipeline vertex data");
    }

    // Link model matrix to pipeline (look for "modelMatrices" binding)
    ax::NodeEditor::PinId modelMatrixInputPin = findInputPin(pipelinePtr, "modelMatrices");
    if (modelMatrixInputPin.Get() != 0) {
        Link matrixLink;
        matrixLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        matrixLink.startPin = modelNode->modelMatrixPin.id;
        matrixLink.endPin = modelMatrixInputPin;
        graph.addLink(matrixLink);
        Log::debug("DefaultRenderer", "Linked: Model matrix -> Pipeline modelMatrices");
    }

    // Link model texture to pipeline (look for "texSampler" binding)
    ax::NodeEditor::PinId texSamplerPin = findInputPin(pipelinePtr, "texSampler");
    if (texSamplerPin.Get() != 0) {
        Link texLink;
        texLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        texLink.startPin = modelNode->texturePin.id;
        texLink.endPin = texSamplerPin;
        graph.addLink(texLink);
        Log::debug("DefaultRenderer", "Linked: Model texture -> Pipeline texSampler");
    }

    // Link camera to pipeline (either GLTF camera from model or fixed camera)
    if (pipelinePtr->hasCameraInput && cameraPinToConnect.Get() != 0) {
        Link cameraLink;
        cameraLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        cameraLink.startPin = cameraPinToConnect;
        cameraLink.endPin = pipelinePtr->cameraInput.pin.id;
        graph.addLink(cameraLink);
        Log::debug("DefaultRenderer", "Linked: Camera -> Pipeline camera input (GLTF: {})", useGLTFCamera);
    }

    // Link light to pipeline
    if (pipelinePtr->hasLightInput) {
        Link lightLink;
        lightLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        lightLink.startPin = lightPtr->lightArrayPin.id;
        lightLink.endPin = pipelinePtr->lightInput.pin.id;
        graph.addLink(lightLink);
        Log::debug("DefaultRenderer", "Linked: Light -> Pipeline light input");
    }

    // Link pipeline output (SV_Target) to present
    ax::NodeEditor::PinId colorOutputPin = findOutputPin(pipelinePtr, "SV_Target");
    if (colorOutputPin.Get() == 0) {
        // Try finding any output pin
        if (!pipelinePtr->shaderReflection.attachmentConfigs.empty()) {
            colorOutputPin = pipelinePtr->shaderReflection.attachmentConfigs[0].pin.id;
        }
    }

    if (colorOutputPin.Get() != 0) {
        Link presentLink;
        presentLink.id = ax::NodeEditor::LinkId(Node::GetNextGlobalId());
        presentLink.startPin = colorOutputPin;
        presentLink.endPin = presentPtr->imagePin.id;
        graph.addLink(presentLink);
        Log::debug("DefaultRenderer", "Linked: Pipeline output -> Present");
    }

    Log::info("DefaultRenderer", "Default renderer created successfully with {} nodes and {} links",
              4, graph.links.size());

    return true;
}

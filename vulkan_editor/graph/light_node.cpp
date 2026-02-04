#include "light_node.h"
#include "node_graph.h"
#include "../util/logger.h"
#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include "node.h"
#include <cmath>
#include <imgui.h>

namespace {
constexpr float PADDING_X = 10.0f;
}

LightNode::LightNode()
    : Node() {
    name = "Lights";
    createDefaultPins();
    ensureLightCount();
}

LightNode::LightNode(int id)
    : Node(id) {
    name = "Lights";
    createDefaultPins();
    ensureLightCount();
}

LightNode::~LightNode() {}

nlohmann::json LightNode::toJson() const {
    nlohmann::json j;

    // Node base info
    j["type"] = "light";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};

    // Light parameters
    j["numLights"] = numLights;
    j["shaderControlledCount"] = shaderControlledCount;

    // Serialize light data array
    nlohmann::json lightsJson = nlohmann::json::array();
    for (const auto& light : lights) {
        lightsJson.push_back(
            {{"position",
              {light.position.x, light.position.y, light.position.z}},
             {"color", {light.color.x, light.color.y, light.color.z}},
             {"radius", light.radius}}
        );
    }
    j["lights"] = lightsJson;

    // Pin info
    j["outputPins"] = {
        {{"id", lightArrayPin.id.Get()},
         {"type", static_cast<int>(lightArrayPin.type)},
         {"label", lightArrayPin.label}}
    };

    return j;
}

void LightNode::fromJson(const nlohmann::json& j) {
    // Node base info
    name = j.value("name", "Lights");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }

    // Light parameters
    numLights = j.value("numLights", 1);
    shaderControlledCount = j.value("shaderControlledCount", false);

    // Restore lights array
    if (j.contains("lights") && j["lights"].is_array()) {
        lights.clear();
        for (const auto& jLight : j["lights"]) {
            LightData light;
            if (jLight.contains("position") &&
                jLight["position"].is_array()) {
                light.position = glm::vec3(
                    jLight["position"][0].get<float>(),
                    jLight["position"][1].get<float>(),
                    jLight["position"][2].get<float>()
                );
            }
            if (jLight.contains("color") &&
                jLight["color"].is_array()) {
                light.color = glm::vec3(
                    jLight["color"][0].get<float>(),
                    jLight["color"][1].get<float>(),
                    jLight["color"][2].get<float>()
                );
            }
            light.radius = jLight.value("radius", 1.0f);
            lights.push_back(light);
        }
    } else {
        ensureLightCount();
    }

    // Restore pin ID
    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        for (const auto& pinJson : j["outputPins"]) {
            if (pinJson.value("label", "") == "light") {
                lightArrayPin.id =
                    ax::NodeEditor::PinId(pinJson["id"].get<int>());
            }
        }
    }
}

void LightNode::createDefaultPins() {
    lightArrayPin.id = ax::NodeEditor::PinId(GetNextGlobalId());
    lightArrayPin.type = PinType::UniformBuffer;
    lightArrayPin.label = "light";
}

void LightNode::ensureLightCount() {
    if (lights.size() != static_cast<size_t>(numLights)) {
        lights.resize(numLights);

        // Position lights in a circle by default
        for (int i = 0; i < numLights; ++i) {
            float angle = (float)i / (float)numLights * 2.0f * M_PI;
            float radius = 5.0f;

            lights[i].position = glm::vec3(
                cos(angle) * radius, 2.0f, sin(angle) * radius
            );
            lights[i].color = glm::vec3(1.0f, 1.0f, 1.0f);
            lights[i].radius = 5.0f;
        }
    }
}

void LightNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    namespace ed = ax::NodeEditor;
    std::vector<std::string> pinLabels = {lightArrayPin.label};
    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(180, 155, 55, 80));

    builder.Begin(id);
    builder.Header(
        ImColor(255, 220, 80)
    ); // Yellow header for light nodes

    float availWidth = nodeWidth - PADDING_X * 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    if (!isRenaming) {
        if (textSize.x < availWidth) {
            float centerOffset = (availWidth - textSize.x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            const_cast<LightNode*>(this)->isRenaming = true;
        }
    } else {
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText(
            "##NodeName", nameBuffer, sizeof(nameBuffer),
            ImGuiInputTextFlags_AutoSelectAll
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<LightNode*>(this)->name = nameBuffer;
            const_cast<LightNode*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    DrawOutputPin(
        lightArrayPin.id, lightArrayPin.label,
        static_cast<int>(lightArrayPin.type),
        graph.isPinLinked(lightArrayPin.id), nodeWidth, builder
    );

    builder.End();
    ed::PopStyleColor();
}

void LightNode::clearPrimitives() {
    lightUbo = nullptr;
    lightPrimitive = nullptr;
    lightUboArray = {};
}

void LightNode::createPrimitives(primitives::Store& store) {
    ensureLightCount();

    // Create single UniformBuffer primitive containing all lights
    primitives::StoreHandle hUbo = store.newUniformBuffer();
    lightUbo = &store.uniformBuffers[hUbo.handle];

    // Mark this UBO as containing light data
    lightUbo->dataType = primitives::UniformDataType::Light;

    // Point to our persistent light array
    lightUbo->data = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(lights.data()),
        sizeof(LightData) * lights.size()
    );

    Log::debug(
        "LightNode", "Holding {} lights in UBO of size {} bytes",
        lights.size(),
        lightUbo->data.size()
    );

    // Create Light primitive for code generation
    primitives::StoreHandle hLight = store.newLight();
    lightPrimitive = &store.lights[hLight.handle];
    lightPrimitive->name = name;  // Use node name for light
    lightPrimitive->ubo = hUbo;
    lightPrimitive->numLights = numLights;

    // Copy light data for code generation
    lightPrimitive->lights.resize(lights.size());
    for (size_t i = 0; i < lights.size(); ++i) {
        lightPrimitive->lights[i].position = lights[i].position;
        lightPrimitive->lights[i].color = lights[i].color;
        lightPrimitive->lights[i].radius = lights[i].radius;
    }

    // Create array with single UBO
    lightUboArray = store.newArray();
    auto& array = store.arrays[lightUboArray.handle];
    array.type = primitives::Type::UniformBuffer;
    array.handles = {hUbo.handle};

    Log::debug(
        "LightNode", "Created light array UBO and Light primitive with {} lights",
        lights.size()
    );
}

void LightNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::StoreHandle>>& outputs
) const {
    if (lightUboArray.isValid()) {
        outputs.push_back({lightArrayPin.id, lightUboArray});
    }
}
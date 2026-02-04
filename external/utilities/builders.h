//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# pragma once


//------------------------------------------------------------------------------
# include <imgui_node_editor.h>

// Gigi changes begin
// I updated the node editor library to a docking branch of imgui and these functions are not part of standard imgui.
namespace ImGui
{
    [[maybe_unused]]
    static void Spring(float weight = 1.0f, float spacing = -1.0f)
    {
    }

    [[maybe_unused]]
    static void BeginHorizontal(const char* str_id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void BeginHorizontal(const void* ptr_id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void BeginHorizontal(int id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void EndHorizontal()
    {
    }

    [[maybe_unused]]
    static void BeginVertical(const char* str_id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void BeginVertical(const void* ptr_id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void BeginVertical(int id, const ImVec2& size = ImVec2(0, 0), float align = -1)
    {
    }

    [[maybe_unused]]
    static void EndVertical()
    {
    }
};
// Gigi changes end

//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {
namespace Utilities {


//------------------------------------------------------------------------------
struct BlueprintNodeBuilder
{
    BlueprintNodeBuilder(ImTextureID texture = 0, int textureWidth = 0, int textureHeight = 0);

    void Begin(NodeId id);
    void End();

    void Header(const ImVec4& color = ImVec4(1, 1, 1, 1));
    void EndHeader();

    void Input(PinId id);
    void EndInput();

    void Middle();

    void Output(PinId id);
    void EndOutput();


private:
    enum class Stage
    {
        Invalid,
        Begin,
        Header,
        Content,
        Input,
        Output,
        Middle,
        End
    };

    bool SetStage(Stage stage);

    void Pin(PinId id, ax::NodeEditor::PinKind kind);
    void EndPin();

    ImTextureID HeaderTextureId;
    int         HeaderTextureWidth;
    int         HeaderTextureHeight;
    NodeId      CurrentNodeId;
    Stage       CurrentStage;
    ImU32       HeaderColor;
    ImVec2      NodeMin;
    ImVec2      NodeMax;
    ImVec2      HeaderMin;
    ImVec2      HeaderMax;
    ImVec2      ContentMin;
    ImVec2      ContentMax;
    bool        HasHeader;
};



//------------------------------------------------------------------------------
} // namespace Utilities
} // namespace Editor
} // namespace ax
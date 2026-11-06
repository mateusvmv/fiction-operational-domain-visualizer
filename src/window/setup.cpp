#include "../window.hpp"
#include "imgui.h"
#include "imgui_internal.h"

void SetupDock(WindowState &win_state)
{
    ImGuiID dockspace_id = win_state.dockspace_id;
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    // Split the dockspace into layout regions (Leaving out the toolbar since it's pinned globally)
    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.30f, nullptr, &dock_main_id);
    ImGuiID dock_right_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.40f, nullptr, &dock_main_id);

    // Assign windows to specific dock nodes
    ImGui::DockBuilderDockWindow("SiDB Plot Control Panel", dock_left_id);
    ImGui::DockBuilderDockWindow("Experiment Results", dock_right_bottom_id);
    ImGui::DockBuilderDockWindow("Operational Domain Plot", dock_main_id);

    ImGui::DockBuilderFinish(dockspace_id);
}
#include "../app.hpp"

#include <filesystem>

namespace fs = std::filesystem;

bool BeginFileBrowserPopup(const std::string &name)
{
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Appearing);
    return ImGui::BeginPopupModal(name.c_str(), nullptr, ImGuiWindowFlags_None);
}

void DrawFileBrowserBody(char* save_path_buffer, std::size_t buffer_size) {
    ImGui::Text("Directory Browser");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##Path", save_path_buffer, buffer_size);

    fs::path current_file_path(save_path_buffer);
    fs::path browse_dir = current_file_path.has_parent_path() && !current_file_path.filename().empty()
                          ? current_file_path.parent_path()
                          : (fs::is_directory(current_file_path) ? current_file_path : fs::current_path());

    static fs::path cached_dir = "";
    static std::vector<fs::directory_entry> cached_entries;
    
    if (browse_dir != cached_dir) {
        cached_dir = browse_dir;
        cached_entries.clear();
        try {
            if (!cached_dir.empty() && fs::exists(cached_dir) && fs::is_directory(cached_dir)) {
                for (const auto& entry : fs::directory_iterator(cached_dir)) {
                    cached_entries.push_back(entry);
                }
            }
        } catch (...) {}
    }

    static std::string selected_entry_name = "";

    if (ImGui::BeginChild("FileList", ImVec2(350, 200), true)) {
        if (!browse_dir.empty() && browse_dir.has_parent_path() && ImGui::Selectable(".. [Parent Directory]", false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                fs::path parent = browse_dir.parent_path();
                fs::path filename = current_file_path.filename();
                fs::path new_full_path = parent / (filename.empty() ? "operational_results.csv" : filename);
                std::strncpy(save_path_buffer, new_full_path.string().c_str(), buffer_size);
            }
        }

        for (const auto& entry : cached_entries) {
            std::string filename = entry.path().filename().string();
            bool is_dir = entry.is_directory();
            std::string display_name = is_dir ? "[DIR] " + filename : filename;

            bool is_selected = (selected_entry_name == filename);
            if (ImGui::Selectable(display_name.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_entry_name = filename;
                if (is_dir) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        fs::path target = entry.path();
                        fs::path filename_curr = current_file_path.filename();
                        fs::path new_full_path = target / (filename_curr.empty() ? "operational_results.csv" : filename_curr);
                        std::strncpy(save_path_buffer, new_full_path.string().c_str(), buffer_size);
                        selected_entry_name = "";
                    }
                } else {
                    fs::path full_file_path = browse_dir / filename;
                    std::strncpy(save_path_buffer, full_file_path.string().c_str(), buffer_size);
                }
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Open")) {
        if (!selected_entry_name.empty()) {
            fs::path target = browse_dir / selected_entry_name;
            if (fs::is_directory(target)) {
                fs::path filename_curr = current_file_path.filename();
                fs::path new_full_path = target / (filename_curr.empty() ? "operational_results.csv" : filename_curr);
                std::strncpy(save_path_buffer, new_full_path.string().c_str(), buffer_size);
                selected_entry_name = "";
            }
        }
    }
}

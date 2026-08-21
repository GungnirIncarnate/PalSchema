#include "Utility/LoadOrderSystem/ModLoadOrderGui.h"
#include <imgui.h>

namespace PS {
    ModLoadOrderGui::ModLoadOrderGui(const Palworld::PalMainLoader& mainLoader)
        : m_mainLoader(mainLoader)
    {
    }

    void ModLoadOrderGui::RenderMods() const
    {
        if (!ImGui::CollapsingHeader("Mods"))
        {
            return;
        }

        const auto plan = m_mainLoader.GetModLoadPlanSnapshot();
        if (plan.displayEntries.empty())
        {
            ImGui::TextUnformatted("Mod load plan is not available yet.");
            return;
        }

        auto getStatusText = [](PS::ModLoadStatus status) {
            switch (status)
            {
            case PS::ModLoadStatus::Loaded:
                return "Loaded";
            case PS::ModLoadStatus::Disabled:
            case PS::ModLoadStatus::DuplicateId:
            case PS::ModLoadStatus::MissingDependency:
            case PS::ModLoadStatus::DependencySkipped:
            case PS::ModLoadStatus::LoadFailed:
            case PS::ModLoadStatus::LoadOrderConflict:
                return "Unloaded";
            }

            return "Unknown";
        };

        if (ImGui::BeginTable("PalSchemaMods", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Mod ID");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Mod Folder");
            ImGui::TableSetupColumn("Reason");
            ImGui::TableHeadersRow();

            for (const auto& entry : plan.displayEntries)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.mod.modId.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(getStatusText(entry.status));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.mod.folderName.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(entry.reason.c_str());
            }

            ImGui::EndTable();
        }
    }
}

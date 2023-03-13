#include "kcpstats.h"
#include "client/imgui.h"
#include "imgui/implot.h"
#include "core/hooks.h"
#include "shared/kcp.h"

AUTOHOOK_INIT()

const char* KCP_NETGRAPH_LABELS[] = {"SRTT", "RTO", "LOS%", "RTS%"};

void draw_kcp_stats()
{
	if (Cvar_kcp_stats == nullptr || !Cvar_kcp_stats->GetBool() || !g_kcp_initialized())
	{
		return;
	}

	auto kcp_stats = g_kcp_manager->get_stats();

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoDecoration;
	window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
	window_flags |= ImGuiWindowFlags_NoResize;
	window_flags |= ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowFocus();
	ImGui::SetNextWindowBgAlpha(0.2f);
	ImGui::Begin("KCP Stats", NULL, window_flags);
	if (kcp_stats.size() == 1)
	{
		if (ImGui::BeginTable("kcp_stats", 8))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[0]);
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 124, 150));
			ImGui::TableNextColumn();
			ImGui::Text("%d", kcp_stats[0].second.srtt);
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[1]);
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 124, 150));
			ImGui::TableNextColumn();
			ImGui::Text("%d", kcp_stats[0].second.rto);
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[2]);
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 124, 150));
			ImGui::TableNextColumn();
			ImGui::Text(
				"%.2f", 100.0 * kcp_stats[0].second.lost_segs / (kcp_stats[0].second.out_segs == 0 ? 1 : kcp_stats[0].second.out_segs));
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[3]);
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 124, 150));
			ImGui::TableNextColumn();
			ImGui::Text(
				"%.2f", 100.0 * kcp_stats[0].second.retrans_segs / (kcp_stats[0].second.out_segs == 0 ? 1 : kcp_stats[0].second.out_segs));
			ImGui::EndTable();
		}
	}
	else if (kcp_stats.size() > 1)
	{
		if (ImGui::BeginTable("kcp_stats", 4))
		{
			for (int col = 0; col < 4; col++)
			{
				ImGui::TableSetupColumn(KCP_NETGRAPH_LABELS[col]);
			}
			ImGui::TableHeadersRow();
			for (const auto& entry : kcp_stats)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.second.srtt);
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.second.rto);
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", 100.0 * entry.second.lost_segs / (entry.second.out_segs == 0 ? 1 : entry.second.out_segs));
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", 100.0 * entry.second.retrans_segs / (entry.second.out_segs == 0 ? 1 : entry.second.out_segs));
			}
			ImGui::EndTable();
		}
	}
	auto current_size = ImGui::GetWindowSize();
	auto main_viewport = ImGui::GetMainViewport();
	auto viewport_pos = main_viewport->WorkPos;
	auto viewport_size = main_viewport->WorkSize;
	ImGui::SetWindowPos(ImVec2(viewport_pos.x + viewport_size.x - current_size.x, viewport_pos.y));
	ImGui::End();
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	imgui_add_draw(draw_kcp_stats);
}

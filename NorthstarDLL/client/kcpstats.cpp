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

	ImGui::Begin("KCP Stats");
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
	ImGui::End();
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	imgui_add_draw(draw_kcp_stats);
}

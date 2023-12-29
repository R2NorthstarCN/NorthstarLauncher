#include "client/igig/igig.h"
#include "imgui/imgui.h"
#include "imgui/implot.h"
#include "core/hooks.h"
#include "shared/ikcp.h"
#include "shared/kcpintegration.h"
#include "engine/netgraph.h"
#include "client/fontawesome.h"

ConVar* Cvar_kcp_stats;

AUTOHOOK_INIT()

static std::vector<double> convert_to_inflines(const std::vector<double>& ys) {
	std::vector<double> result;
	for (int i = 0; i < ys.size(); i++)
	{
		if (ys[i] > 0.001)
		{
			result.push_back(ys.size() - 1 - i);
		}
	}
	return result;
}

static void draw_kcp_graph(float horizontal)
{
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(120, 120, 120, 0));
	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(0, 0, 0, 160));

	if (ImPlot::BeginPlot("##NetGraph", ImVec2(horizontal, horizontal / 3), ImPlotFlags_CanvasOnly))
	{
		auto ng = NetGraphSink::instance();
		auto& entry = *ng->windows.begin();
		auto& localSlidingWindows = std::get<2>(entry.second);
		auto& remoteSlidingWindows = std::get<3>(entry.second);

		ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
		ImPlot::SetupAxis(ImAxis_Y1, NULL, ImPlotAxisFlags_NoGridLines);
		ImPlot::SetupAxis(ImAxis_Y2, NULL, ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
		
		auto srtt = std::get<1>(entry.second).get_axes();
		ImPlot::SetupAxisLimits(ImAxis_Y1, 0, std::max(std::get<1>(entry.second).max(), 50.0), ImPlotCond_Always);
		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
		ImPlot::PlotLine("SRTT", srtt.first.data(), srtt.second.data(), srtt.first.size());
		
		auto sv = remoteSlidingWindows.sw_sv.get_axes();
		ImPlot::SetupAxisLimits(ImAxis_Y2, 0, std::max(remoteSlidingWindows.sw_sv.max(), 60.0), ImPlotCond_Always);
		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
		ImPlot::PlotLine("SV", sv.first.data(), sv.second.data(), sv.first.size());

		// RECON
		ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 1, 0, 1));
		auto outbound_recon = convert_to_inflines(localSlidingWindows.sw_reconsegs.get_axes().second);
		ImPlot::PlotInfLinesEx("OUT_RECON", outbound_recon.data(), outbound_recon.size(), ImPlotInfLinesExFlags_FirstHalf);
		auto inbound_recon = convert_to_inflines(remoteSlidingWindows.sw_reconsegs.get_axes().second);
		ImPlot::PlotInfLinesEx("IN_RECON", inbound_recon.data(), inbound_recon.size(), ImPlotInfLinesExFlags_SecondHalf);
		ImPlot::PopStyleColor();

		// LOST
		ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0, 0, 1));
		auto outbound_lost = convert_to_inflines(localSlidingWindows.sw_lostsegs.get_axes().second);
		ImPlot::PlotInfLinesEx("OUT_LOST", outbound_lost.data(), outbound_lost.size(), ImPlotInfLinesExFlags_FirstHalf);
		auto inbound_lost = convert_to_inflines(remoteSlidingWindows.sw_lostsegs.get_axes().second);
		ImPlot::PlotInfLinesEx("IN_LOST", inbound_lost.data(), inbound_lost.size(), ImPlotInfLinesExFlags_SecondHalf);
		ImPlot::PopStyleColor();

		// RETRANS
		ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0.65, 0, 1));
		auto outbound_retrans = convert_to_inflines(localSlidingWindows.sw_retranssegs.get_axes().second);
		ImPlot::PlotInfLinesEx("OUT_RETRANS", outbound_retrans.data(), outbound_retrans.size(), ImPlotInfLinesExFlags_FirstHalf);
		auto inbound_retrans = convert_to_inflines(remoteSlidingWindows.sw_retranssegs.get_axes().second);
		ImPlot::PlotInfLinesEx("IN_RETRANS", inbound_retrans.data(), inbound_retrans.size(), ImPlotInfLinesExFlags_SecondHalf);
		ImPlot::PopStyleColor();

		ImPlot::EndPlot();
	}
}

namespace ImGui
{
	void TableCellHeaderBg()
	{
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(30, 30, 30, 160));
	}

	void TableCellValueBg()
	{
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(0, 0, 0, 140));
	}
}

static void draw_kcp_table()
{
	if (ImGui::BeginTable("kcp_stats", 8))
	{
		auto ng = NetGraphSink::instance();
		auto& entry = *ng->windows.begin();
		auto& localSlidingWindows = std::get<2>(entry.second);
		auto& remoteSlidingWindows = std::get<3>(entry.second);
		auto localOutSegsSum = localSlidingWindows.sw_outsegs.sum() + 0.00001;
		auto localInSegsSum = localSlidingWindows.sw_insegs.sum() + 0.00001;
		auto remoteOutSegsSum = remoteSlidingWindows.sw_outsegs.sum() + 0.00001;
		auto remoteInSegsSum = remoteSlidingWindows.sw_insegs.sum() + 0.00001;

		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "   SV");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f", remoteSlidingWindows.sw_sv.last());

		// Outbound

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "\xef\x8d\x9b LOS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f", localSlidingWindows.sw_lostsegs.sum() / localOutSegsSum * 100.0);

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "RTS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f", localSlidingWindows.sw_retranssegs.sum() / localOutSegsSum * 100.0);

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "RCS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f ", remoteSlidingWindows.sw_reconsegs.sum() / remoteInSegsSum * 100.0);

		// ------------------------------------------------------------------------------

		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", " SRTT");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%d", (IINT32)std::get<1>(entry.second).last());

		// Inbound

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "\xef\x8d\x98 LOS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f", remoteSlidingWindows.sw_lostsegs.sum() / remoteOutSegsSum * 100.0);

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "RTS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f", remoteSlidingWindows.sw_retranssegs.sum() / remoteOutSegsSum * 100.0);

		ImGui::TableNextColumn();
		ImGui::TableCellHeaderBg();
		ImGui::Text("%s", "RCS%");

		ImGui::TableNextColumn();
		ImGui::TableCellValueBg();
		ImGui::Text("%.2f ", localSlidingWindows.sw_reconsegs.sum() / localInSegsSum * 100.0);

		ImGui::EndTable();
	}
}

static void draw_kcp_stats()
{
	auto lvl = Cvar_kcp_stats->GetInt();

	if (Cvar_kcp_stats == nullptr || lvl <= 0)
	{
		return;
	}

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoDecoration;
	window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
	window_flags |= ImGuiWindowFlags_NoResize;
	window_flags |= ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBackground;
	window_flags |= ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0);

	if (!ImGui::Begin("KCP Stats", NULL, window_flags))
	{
		ImGui::End();
	}

	auto current_size = ImGui::GetWindowSize();
	auto main_viewport = ImGui::GetMainViewport();
	auto viewport_pos = main_viewport->WorkPos;
	auto viewport_size = main_viewport->WorkSize;
	ImGui::SetWindowPos(ImVec2(viewport_pos.x + viewport_size.x - current_size.x, viewport_pos.y + viewport_size.y - current_size.y));

	auto ng = NetGraphSink::instance();
	std::shared_lock lk(ng->windowsMutex);

	if (ng->windows.empty())
	{
		ImGui::Text("No connection");
	}
	else
	{
		if (lvl >= 2)
		{
			draw_kcp_graph(current_size.x * 0.95);
		}
		if (lvl >= 1)
		{
			draw_kcp_table();
		}
	}

	ImGui::End();

	ImGui::PopStyleVar(6);

	if (lvl == 5)
	{
		ImGui::ShowMetricsWindow();
	}
	else if (lvl == 6)
	{
		ImPlot::ShowMetricsWindow();
	}
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	ImGuiManager::instance().addDrawFunction(draw_kcp_stats);
}

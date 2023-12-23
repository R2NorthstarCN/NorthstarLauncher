#include "kcpstats.h"
#include "client/imgui.h"
#include "imgui/implot.h"
#include "core/hooks.h"
#include "shared/ikcp.h"
#include "shared/kcpintegration.h"
#include "engine/netgraph.h"
#include "fontawesome.h"
#include "imgui/imgui_internal.h"

ConVar* Cvar_kcp_stats;

AUTOHOOK_INIT()

void draw_kcp_stats(ID3D11Device* device)
{
	if (Cvar_kcp_stats == nullptr || !Cvar_kcp_stats->GetBool())
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

	ImGui::Begin("KCP Stats", NULL, window_flags);

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
		if (ImGui::BeginTable("kcp_stats", 7))
		{
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
			ImGui::Text("%s", " SRTT");

			// Outbound

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "\xef\x8d\x9bLOS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", localSlidingWindows.sw_lostsegs.sum() / localOutSegsSum);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RTS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", localSlidingWindows.sw_retranssegs.sum() / localOutSegsSum);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RCS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f ", remoteSlidingWindows.sw_reconsegs.sum() / remoteInSegsSum);

			// ------------------------------------------------------------------------------

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", std::get<1>(entry.second).last());

			// Inbound

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "\xef\x8d\x98LOS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", remoteSlidingWindows.sw_lostsegs.sum() / remoteOutSegsSum);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RTS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", remoteSlidingWindows.sw_retranssegs.sum() / remoteOutSegsSum);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RCS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f ", localSlidingWindows.sw_reconsegs.sum() / localInSegsSum);

			ImGui::EndTable();
		}
		/*
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(120, 120, 120, 102));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(0, 0, 0, 160));

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));

		if (ImPlot::BeginPlot("##SRTT", ImVec2(160, 100), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
		{
			ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
			ImPlot::SetupAxis(ImAxis_Y1, NULL);

			auto y_max = sw_srtt.max();
			double y_avg = sw_srtt.avg();

			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_max, ImPlotCond_Always);

			y_avg < 30	  ? ImPlot::PushGreenLineColor()
			: y_avg < 60  ? ImPlot::PushLimeLineColor()
			: y_avg < 100 ? ImPlot::PushYellowLineColor()
			: y_avg < 150 ? ImPlot::PushOrangeLineColor()
			: y_avg < 200 ? ImPlot::PushRedLineColor()
						  : ImPlot::PushPurpleLineColor();

			auto data = sw_srtt.get_smoothed_axes();
			ImPlot::PlotLine("SRTT", data.first.data(), data.second.data(), data.first.size());
			ImPlot::PopStyleColor();

			ImPlot::PushAvgLineColor();
			ImPlot::PlotInfLines("AVG", &y_avg, 1, ImPlotInfLinesFlags_Horizontal);
			ImPlot::PopStyleColor();

			ImPlot::PlotText("SRTT", data.first.size() / 2.0, y_max, ImVec2(0, 7));
			ImPlot::EndPlot();
		}

		ImGui::SameLine();

		if (ImPlot::BeginPlot("##RTS%", ImVec2(160, 100), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
		{
			ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
			ImPlot::SetupAxis(ImAxis_Y1, NULL);

			auto y_max = sw_rts.max();
			double y_avg = sw_rts.avg();

			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_max, ImPlotCond_Always);

			y_avg < 3	 ? ImPlot::PushGreenLineColor()
			: y_avg < 6	 ? ImPlot::PushLimeLineColor()
			: y_avg < 10 ? ImPlot::PushYellowLineColor()
			: y_avg < 15 ? ImPlot::PushOrangeLineColor()
			: y_avg < 20 ? ImPlot::PushRedLineColor()
						 : ImPlot::PushPurpleLineColor();

			auto data = sw_rts.get_smoothed_axes();
			ImPlot::PlotLine("RTS%", data.first.data(), data.second.data(), data.first.size());
			ImPlot::PopStyleColor();

			ImPlot::PushAvgLineColor();
			ImPlot::PlotInfLines("AVG", &y_avg, 1, ImPlotInfLinesFlags_Horizontal);
			ImPlot::PopStyleColor();

			ImPlot::PlotText("RTS%", data.first.size() / 2.0, y_max, ImVec2(0, 7));
			ImPlot::EndPlot();
		}

		ImGui::PopStyleVar();

		ImPlot::PopStyleColor(2);*/
	}
	ImGui::End();
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	imgui_add_draw(draw_kcp_stats);
}

sliding_window::sliding_window(size_t samples)
{
	raw = std::vector<double>(samples);
	for (int i = 0; i < raw.size(); ++i)
	{
		axis_x.push_back(i);
	}
}

sliding_window::sliding_window(size_t samples, bool delta)
{
	this->delta = delta;
	raw = std::vector<double>(samples);
	for (int i = 0; i < raw.size(); ++i)
	{
		axis_x.push_back(i);
	}
}

void sliding_window::rotate(double new_val)
{
	sumed -= *(raw.end() - 1);
	std::rotate(raw.rbegin(), raw.rbegin() + 1, raw.rend());
	if (delta)
	{
		raw[0] = new_val - last_val;
		last_val = new_val;
	}
	else
	{
		raw[0] = new_val;
	}
	sumed += raw[0];
}

std::pair<std::vector<double>&, std::vector<double>&> sliding_window::get_axes()
{
	return std::make_pair(std::ref(axis_x), std::ref(raw));
}

double sliding_window::last() const
{
	return raw[0];
}

double sliding_window::avg() const
{
	return sumed / raw.size();
}

double sliding_window::sum() const
{
	return sumed;
}

double sliding_window::max() const
{
	return *std::max_element(raw.begin(), raw.end());
}

double sliding_window::min() const
{
	return *std::min_element(raw.begin(), raw.end());
}

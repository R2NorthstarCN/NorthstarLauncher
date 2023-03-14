#include "kcpstats.h"
#include "client/imgui.h"
#include "imgui/implot.h"
#include "core/hooks.h"
#include "shared/kcp.h"

AUTOHOOK_INIT()

static inline void itimeofday(long* sec, long* usec)
{
	static long mode = 0, addsec = 0;
	BOOL retval;
	static IINT64 freq = 1;
	IINT64 qpc;
	if (mode == 0)
	{
		retval = QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
		freq = (freq == 0) ? 1 : freq;
		retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
		addsec = (long)time(NULL);
		addsec = addsec - (long)((qpc / freq) & 0x7fffffff);
		mode = 1;
	}
	retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
	retval = retval * 2;
	if (sec)
		*sec = (long)(qpc / freq) + addsec;
	if (usec)
		*usec = (long)((qpc % freq) * 1000000 / freq);
}

/* get clock in millisecond 64 */
static inline IINT64 iclock64(void)
{
	long s, u;
	IINT64 value;
	itimeofday(&s, &u);
	value = ((IINT64)s) * 1000 + (u / 1000);
	return value;
}

static inline IUINT32 iclock()
{
	return (IUINT32)(iclock64() & 0xfffffffful);
}

static inline IINT32 itimediff(IUINT32 later, IUINT32 earlier)
{
	return ((IINT32)(later - earlier));
}

const char* KCP_NETGRAPH_LABELS[] = {" SRTT", "RTO", "LOS%", "RTS%"};

#define KCP_SET_HEADER_BG ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 124, 150))
#define KCP_SET_VALUE_BG ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(68, 67, 67, 102))

#define KCP_PURPLE_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(127, 0, 255, 255))
#define KCP_RED_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 0, 0, 255))
#define KCP_ORANGE_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 128, 0, 255))
#define KCP_YELLOW_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 255, 0, 255))
#define KCP_LIME_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(128, 255, 0, 255))
#define KCP_GREEN_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(0, 255, 0, 255))

std::vector<kcp_stats> kcp_stats_sliding_window(50);
std::vector<double> rts_sliding_window(50);
IUINT32 last_rotate = iclock();

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
	window_flags |= ImGuiWindowFlags_NoBackground;
	window_flags |= ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowFocus();
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
	ImGui::SetWindowPos(ImVec2(viewport_pos.x + viewport_size.x - current_size.x, viewport_pos.y));

	if (kcp_stats.size() == 1)
	{
		std::vector<double> xs;
		std::vector<double> y_srtts;
		std::vector<double> y_rtss;
		IINT32 y_srtt_max = 0;
		double y_rts_max = 0;
		IUINT32 out_segs = kcp_stats[0].second.out_segs, lost_segs = kcp_stats[0].second.lost_segs,
				retrans_segs = kcp_stats[0].second.retrans_segs;

		for (int i = 0; i < 50; ++i)
		{
			xs.push_back(i);
			y_srtts.push_back(kcp_stats_sliding_window[i].srtt);
			y_srtt_max = std::max(kcp_stats_sliding_window[i].srtt, y_srtt_max);
			y_rtss.push_back(rts_sliding_window[i]);
			y_rts_max = std::max(rts_sliding_window[i], y_rts_max);
			out_segs += kcp_stats_sliding_window[i].out_segs;
			lost_segs += kcp_stats_sliding_window[i].lost_segs;
			retrans_segs += kcp_stats_sliding_window[i].retrans_segs;
		}

		if (itimediff(iclock(), last_rotate) > Cvar_kcp_stats_interval->GetInt())
		{
			std::rotate(kcp_stats_sliding_window.rbegin(), kcp_stats_sliding_window.rbegin() + 1, kcp_stats_sliding_window.rend());
			std::rotate(rts_sliding_window.rbegin(), rts_sliding_window.rbegin() + 1, rts_sliding_window.rend());
			kcp_stats_sliding_window[0] = kcp_stats[0].second;
			rts_sliding_window[0] = 100.0 * retrans_segs / (out_segs == 0 ? 1 : out_segs);
			last_rotate = iclock();
		}

		if (ImGui::BeginTable("kcp_stats", 8))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[0]);
			KCP_SET_HEADER_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%d", kcp_stats[0].second.srtt);
			KCP_SET_VALUE_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[1]);
			KCP_SET_HEADER_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%d", kcp_stats[0].second.rto);
			KCP_SET_VALUE_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[2]);
			KCP_SET_HEADER_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%.2f", 100.0 * lost_segs / (out_segs == 0 ? 1 : out_segs));
			KCP_SET_VALUE_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%s", KCP_NETGRAPH_LABELS[3]);
			KCP_SET_HEADER_BG;
			ImGui::TableNextColumn();
			ImGui::Text("%.2f ", 100.0 * retrans_segs / (out_segs == 0 ? 1 : out_segs));
			KCP_SET_VALUE_BG;
			ImGui::EndTable();
		}

		ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(68, 67, 67, 102));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(68, 67, 67, 160));
		if (ImPlot::BeginPlot("##SRTT", ImVec2(150, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
		{
			ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
			ImPlot::SetupAxis(ImAxis_Y1, NULL);
			ImPlot::SetupAxisLimits(
				ImAxis_Y1,
				0,
				y_srtt_max > 200 ? (KCP_RED_LINE, 400)
				: y_srtt_max > 100 ? (KCP_ORANGE_LINE, 200)
				: y_srtt_max > 50  ? (KCP_LIME_LINE, 100)
								   : (KCP_GREEN_LINE, 50),
				ImPlotCond_Always);
			ImPlot::PlotLine("SRTT", xs.data(), y_srtts.data(), xs.size());
			ImPlot::EndPlot();
		}
		ImGui::SameLine();
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(68, 67, 67, 102));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(68, 67, 67, 160));
		if (ImPlot::BeginPlot("##RTS%", ImVec2(150, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
		{
			ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
			ImPlot::SetupAxis(ImAxis_Y1, NULL);
			ImPlot::SetupAxisLimits(
				ImAxis_Y1,
				0,
				y_rts_max > 50.0   ? (KCP_PURPLE_LINE, 100.0)
				: y_rts_max > 25.0 ? (KCP_RED_LINE, 50.0)
				: y_rts_max > 13.0 ? (KCP_ORANGE_LINE, 25.0)
				: y_rts_max > 6.0  ? (KCP_YELLOW_LINE, 13.0)
				: y_rts_max > 3.0  ? (KCP_LIME_LINE, 6.0)
								   : (KCP_GREEN_LINE, 3.0),
				ImPlotCond_Always);
			ImPlot::PlotLine("RTS%", xs.data(), y_rtss.data(), xs.size());
			ImPlot::EndPlot();
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
	ImGui::End();
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	Cvar_kcp_stats_interval = new ConVar("kcp_stats_interval", "100", FCVAR_NONE, "kcp stats interval");
	imgui_add_draw(draw_kcp_stats);
}

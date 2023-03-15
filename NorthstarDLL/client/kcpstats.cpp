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

const char* KCP_NETGRAPH_LABELS[] = {" SRTT", "LOS%", "RTS%"};

#define KCP_SET_HEADER_BG ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 120, 120, 140))
#define KCP_SET_VALUE_BG ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(0, 0, 0, 140))

#define KCP_PURPLE_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(127, 0, 255, 255))
#define KCP_RED_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 0, 0, 255))
#define KCP_ORANGE_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 128, 0, 255))
#define KCP_YELLOW_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 255, 0, 255))
#define KCP_LIME_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(128, 255, 0, 255))
#define KCP_GREEN_LINE ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(0, 255, 0, 255))

sliding_window sw_srtt(50);
sliding_window sw_retrans_segs(10);
sliding_window sw_lost_segs(10);
sliding_window sw_out_segs(10);
sliding_window sw_rts(50);
sliding_window sw_los(50);

IUINT32 last_rotate = iclock();

void draw_kcp_stats()
{
	if (Cvar_kcp_stats == nullptr || !Cvar_kcp_stats->GetBool() || !g_kcp_initialized())
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

	if (itimediff(iclock(), last_rotate) > Cvar_kcp_stats_interval->GetInt())
	{
		auto kcp_stats = g_kcp_manager->get_stats();

		if (kcp_stats.size() > 0)
		{
			sw_srtt.rotate(kcp_stats[0].second.srtt);
			sw_retrans_segs.rotate_delta(kcp_stats[0].second.retrans_segs);
			sw_lost_segs.rotate_delta(kcp_stats[0].second.lost_segs);
			sw_out_segs.rotate_delta(kcp_stats[0].second.out_segs);
			sw_rts.rotate(100.0 * sw_retrans_segs.sum() / (sw_out_segs.sum() == 0 ? 1 : sw_out_segs.sum()));
			sw_los.rotate(100.0 * sw_lost_segs.sum() / (sw_out_segs.sum() == 0 ? 1 : sw_out_segs.sum()));
		}

		last_rotate = iclock();
	}

	if (ImGui::BeginTable("kcp_stats", 6))
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("%s", KCP_NETGRAPH_LABELS[0]);
		KCP_SET_HEADER_BG;
		ImGui::TableNextColumn();
		ImGui::Text("%d", (int)sw_srtt.lastest());
		KCP_SET_VALUE_BG;
		ImGui::TableNextColumn();
		ImGui::Text("%s", KCP_NETGRAPH_LABELS[1]);
		KCP_SET_HEADER_BG;
		ImGui::TableNextColumn();
		ImGui::Text("%.2f", sw_los.lastest());
		KCP_SET_VALUE_BG;
		ImGui::TableNextColumn();
		ImGui::Text("%s", KCP_NETGRAPH_LABELS[2]);
		KCP_SET_HEADER_BG;
		ImGui::TableNextColumn();
		ImGui::Text("%.2f ", sw_rts.lastest());
		KCP_SET_VALUE_BG;
		ImGui::EndTable();
	}

	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(120, 120, 120, 102));
	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(0, 0, 0, 160));
	if (ImPlot::BeginPlot("##SRTT", ImVec2(150, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
	{
		auto y_srtt_max = sw_srtt.max();
		ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_NoTickLabels);
		ImPlot::SetupAxis(ImAxis_Y1, NULL);
		ImPlot::SetupAxisLimits(
			ImAxis_Y1,
			0,
			y_srtt_max > 200   ? (KCP_RED_LINE, 400)
			: y_srtt_max > 100 ? (KCP_ORANGE_LINE, 200)
			: y_srtt_max > 50  ? (KCP_LIME_LINE, 100)
							   : (KCP_GREEN_LINE, 50),
			ImPlotCond_Always);
		auto data = sw_srtt.get_axes();
		ImPlot::PlotLine("SRTT", data.first.data(), data.second.data(), data.first.size());
		ImPlot::EndPlot();
	}
	ImGui::SameLine();
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(120, 120, 120, 102));
	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(0, 0, 0, 160));
	if (ImPlot::BeginPlot("##RTS%", ImVec2(150, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs))
	{
		auto y_rts_max = sw_rts.max();
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
		auto data = sw_rts.get_axes();
		ImPlot::PlotLine("RTS%", data.first.data(), data.second.data(), data.first.size());
		ImPlot::EndPlot();
	}

	ImGui::End();
}

ON_DLL_LOAD("client.dll", KCPSTATS, (CModule module))
{
	Cvar_kcp_stats = new ConVar("kcp_stats", "0", FCVAR_NONE, "kcp stats window");
	Cvar_kcp_stats_interval = new ConVar("kcp_stats_interval", "100", FCVAR_NONE, "kcp stats interval");
	imgui_add_draw(draw_kcp_stats);
}

sliding_window::sliding_window(size_t samples)
{
	inner = std::vector<double>(samples);
}

void sliding_window::rotate(double new_val)
{
	std::rotate(inner.rbegin(), inner.rbegin() + 1, inner.rend());
	inner[0] = new_val;
}

void sliding_window::rotate_delta(double new_val)
{
	std::rotate(inner.rbegin(), inner.rbegin() + 1, inner.rend());
	inner[0] = new_val - inner[1];
}

std::pair<std::vector<double>, std::vector<double>> sliding_window::get_axes()
{
	std::vector<double> x;
	for (int i = 0; i < inner.size(); ++i)
	{
		x.push_back(i);
	}
	return std::make_pair(x, inner);
}

double sliding_window::lastest()
{
	return inner[0];
}

double sliding_window::avg()
{
	return sum() / inner.size();
}

double sliding_window::sum()
{
	double result = 0;
	for (const auto& e : inner)
	{
		result += e;
	}
	return result;
}

double sliding_window::max()
{
	return *std::max_element(inner.begin(), inner.end());
}

double sliding_window::min()
{
	return *std::min_element(inner.begin(), inner.end());
}

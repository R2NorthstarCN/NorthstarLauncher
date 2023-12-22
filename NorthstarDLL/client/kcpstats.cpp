#include "kcpstats.h"
#include "client/imgui.h"
#include "imgui/implot.h"
#include "core/hooks.h"
#include "shared/ikcp.h"
#include "shared/kcpintegration.h"
#include "engine/netgraph.h"
#include "fontawesome.h"

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
		if (ImGui::BeginTable("kcp_stats", 9))
		{
			auto& entry = *ng->windows.begin();
			auto& localSildingWindows = std::get<2>(entry.second);
			auto& remoteSlidingWindows = std::get<3>(entry.second);
			auto localOutSegsAvg = localSildingWindows.sw_outsegs.avg() + 0.00001;
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "\xef\x8d\x9b");

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "SRTT");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%d", std::get<1>(entry.second).last_val);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "LOS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", localSildingWindows.sw_lostsegs.avg() / localOutSegsAvg);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RTS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f", localSildingWindows.sw_retranssegs.avg() / localOutSegsAvg);

			ImGui::TableNextColumn();
			ImGui::TableCellHeaderBg();
			ImGui::Text("%s", "RCS%");

			ImGui::TableNextColumn();
			ImGui::TableCellValueBg();
			ImGui::Text("%.2f ", 0.0);

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
	smoother = new modified_sinc_smoother(6);
	raw = std::vector<double>(samples);
	smoothed = std::vector<double>(samples);
	for (int i = 0; i < raw.size(); ++i)
	{
		axis_x.push_back(i);
	}
}

sliding_window::sliding_window(size_t samples, bool smooth, bool delta)
{
	this->smooth = smooth;
	this->delta = delta;
	raw = std::vector<double>(samples);
	for (int i = 0; i < raw.size(); ++i)
	{
		axis_x.push_back(i);
	}
}

sliding_window::~sliding_window()
{
	delete smoother;
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
	if (smooth)
	{
		smoothed = smoother->smooth(raw);
	}
}

std::pair<std::vector<double>&, std::vector<double>&> sliding_window::get_axes()
{
	return std::make_pair(std::ref(axis_x), std::ref(raw));
}

std::pair<std::vector<double>&, std::vector<double>&> sliding_window::get_smoothed_axes()
{
	return std::make_pair(std::ref(axis_x), std::ref(smoothed));
}

double sliding_window::latest_smoothed()
{
	return smoothed[0];
}

double sliding_window::avg()
{
	return sumed / raw.size();
}

double sliding_window::sum()
{
	return sumed;
}

double sliding_window::max()
{
	return *std::max_element(raw.begin(), raw.end());
}

double sliding_window::min()
{
	return *std::min_element(raw.begin(), raw.end());
}

void linear_regression::clear()
{
	sum_weights = 0;
	sum_x = 0;
	sum_y = 0;
	sum_xy = 0;
	sum_x2 = 0;
	sum_y2 = 0;
	calculated = false;
}

void linear_regression::add_point_w(double x, double y, double weight)
{
	sum_weights += weight;
	sum_x += weight * x;
	sum_y += weight * y;
	sum_xy += weight * x * y;
	sum_x2 = weight * x * x;
	sum_y2 = weight * y * y;
	calculated = false;
}

void linear_regression::calculate()
{
	double std_x2_times_n = sum_x2 - sum_x * sum_x * (1 / sum_weights);
	double std_y2_times_n = sum_y2 - sum_y * sum_y * (1 / sum_weights);
	if (sum_weights > 0)
	{
		slope = (sum_xy - sum_x * sum_y * (1 / sum_weights)) / std_x2_times_n;
		if (std::isnan(slope))
		{
			slope = 0;
		}
	}
	else
	{
		slope = std::numeric_limits<double>::quiet_NaN();
	}
	offset = (sum_y - slope * sum_x) / sum_weights;
	calculated = true;
}

double linear_regression::get_offset()
{
	if (!calculated)
	{
		calculate();
	}
	return offset;
}

double linear_regression::get_slope()
{
	if (!calculated)
	{
		calculate();
	}
	return slope;
}

int bandwidth_to_m(int degree, double bandwidth)
{
	double radius = (0.74548 + 0.24943 * degree) / bandwidth - 1.0;
	return (int)std::round(radius);
}

std::vector<double> get_coeffs(int degree, int m)
{
	std::vector<std::vector<double>>& corr_for_deg = CORRECTION_DATA[degree / 2];
	if (corr_for_deg.size() == 0)
	{
		return std::vector<double>();
	}
	std::vector<double> coeffs(corr_for_deg.size());
	for (int i = 0; i < corr_for_deg.size(); i++)
	{
		double cm = corr_for_deg[i][2] - m;
		coeffs[i] = corr_for_deg[i][0] + corr_for_deg[i][1] / (cm * cm * cm);
	}
	return coeffs;
}

#define M_PI 3.14159265358979323846

std::vector<double> make_kernel(int degree, int m, std::vector<double>& coeffs)
{
	std::vector<double> kernel(m + 1);
	int n_coeffs = coeffs.size();
	double sum = 0;
	for (int i = 0; i <= m; ++i)
	{
		double x = i * (1.0 / (m + 1));
		double sinc_arg = M_PI * 0.5 * (degree + 4) * x;
		double k = i == 0 ? 1 : std::sin(sinc_arg) / sinc_arg;
		for (int j = 0; j < n_coeffs; j++)
		{
			int nu = ((degree / 2) & 0x1) == 0 ? 2 : 1;
			k += coeffs[j] * x * std::sin((2 * j + nu) * M_PI * x);
		}
		double decay = 4;
		k *= std::exp(-x * x * decay) + std::exp(-(x - 2) * (x - 2) * decay) + std::exp(-(x + 2) * (x + 2) * decay) - 2 * std::exp(-decay) -
			 std::exp(-9 * decay);
		kernel[i] = k;
		sum += k;
		if (i > 0)
		{
			sum += k;
		}
	}
	for (int i = 0; i <= m; i++)
	{
		kernel[i] *= 1.0 / sum;
	}
	return kernel;
}

std::vector<double> make_kernel(int degree, int m)
{
	std::vector<double> coeffs = get_coeffs(degree, m);
	return make_kernel(degree, m, coeffs);
}

std::vector<double> make_fit_weights(int degree, int m)
{
	double first_zero = (m + 1) / (1.5 + 0.5 * degree);
	double beta = 0.70 + 0.14 * std::exp(-0.60 * (degree - 4));
	int fit_length = (int)std::ceil(first_zero * beta);
	std::vector<double> weights(fit_length);
	for (int p = 0; p < fit_length; ++p)
	{
		double tmp = std::cos(0.5 * M_PI / (first_zero * beta) * p);
		weights[p] = tmp * tmp;
	}
	return weights;
}

modified_sinc_smoother::modified_sinc_smoother(int degree)
{
	this->degree = degree;
	int m = degree / 2 + 2;
	kernel = make_kernel(degree, m);
	fit_weights = make_fit_weights(degree, m);
}

std::vector<double> modified_sinc_smoother::smooth(std::vector<double>& data)
{
	int radius = kernel.size() - 1;
	auto extended_data = extend_data(data, degree, radius);
	auto extended_smoothed = smooth_except_boudaries(extended_data);
	extended_smoothed.erase(extended_smoothed.begin(), extended_smoothed.begin() + radius);
	extended_smoothed.resize(data.size());
	return extended_smoothed;
}

std::vector<double> modified_sinc_smoother::extend_data(std::vector<double>& data, int degree, int m)
{
	std::vector<double> extended_data(data.size() + 2 * m);
	std::copy(data.begin(), data.end(), extended_data.begin() + m);
	linear_regression lin_reg;
	int fit_length = std::min(fit_weights.size(), data.size());
	for (int p = 0; p < fit_length; ++p)
	{
		lin_reg.add_point_w(p, data[p], fit_weights[p]);
	}
	double offset = lin_reg.get_offset();
	double slope = lin_reg.get_slope();
	for (int p = -1; p >= -m; p--)
	{
		extended_data[m + p] = offset + slope * p;
	}
	lin_reg.clear();
	for (int p = 0; p < fit_length; p++)
	{
		lin_reg.add_point_w(p, data[data.size() - 1 - p], fit_weights[p]);
	}
	offset = lin_reg.get_offset();
	slope = lin_reg.get_slope();
	for (int p = -1; p >= -m; p--)
	{
		extended_data[data.size() + m - 1 - p] = offset + slope * p;
	}
	return extended_data;
}

std::vector<double> modified_sinc_smoother::smooth_except_boudaries(std::vector<double>& data)
{
	std::vector<double> smoothed(data.size());
	int radius = kernel.size() - 1;
	for (int i = radius; i < data.size() - radius; i++)
	{
		double sum = kernel[0] * data[i];
		for (int j = 1; j < kernel.size(); j++)
		{
			sum += kernel[j] * (data[i - j] + data[i + j]);
		}
		smoothed[i] = sum;
	}
	return smoothed;
}

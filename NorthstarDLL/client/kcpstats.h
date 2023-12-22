#pragma once

extern ConVar* Cvar_kcp_stats;
extern ConVar* Cvar_kcp_stats_interval;

class linear_regression
{
  private:
	double sum_weights = 0;
	double sum_x = 0;
	double sum_y = 0;
	double sum_xy = 0;
	double sum_x2 = 0;
	double sum_y2 = 0;
	double offset = std::numeric_limits<double>::quiet_NaN();
	double slope = std::numeric_limits<double>::quiet_NaN();
	bool calculated = false;

  public:
	void clear();
	void add_point_w(double x, double y, double weight);
	void calculate();
	double get_offset();
	double get_slope();
};

const int MAX_DEGREE = 10;
std::vector<std::vector<std::vector<double>>> CORRECTION_DATA {
	{}, // not defined for degree 0
	{}, // no correction required for degree 2
	{}, // no correction required for degree 4
	// data for 6th degree coefficient for flat passband
	{{0.001717576, 0.02437382, 1.64375}},
	// data for 8th degree coefficient for flat passband
	{{0.0043993373, 0.088211164, 2.359375}, {0.006146815, 0.024715371, 3.6359375}},
	// data for 10th degree coefficient for flat passband
	{{0.0011840032, 0.04219344, 2.746875}, {0.0036718843, 0.12780383, 2.7703125}}};

class modified_sinc_smoother
{
  private:
	int degree;
	std::vector<double> kernel;
	std::vector<double> fit_weights;

	std::vector<double> extend_data(std::vector<double>& data, int degree, int m);
	std::vector<double> smooth_except_boudaries(std::vector<double>& data);

  public:
	modified_sinc_smoother(int degree);

	std::vector<double> smooth(std::vector<double>& data);
};

struct sliding_window
{
	std::vector<double> raw;
	modified_sinc_smoother* smoother;

	std::vector<double> axis_x;
	std::vector<double> smoothed;

	bool smooth = true;
	bool delta = false;
	double last_val = 0;
	double sumed = 0;

	sliding_window(size_t samples);
	sliding_window(size_t samples, bool smooth, bool delta);
	~sliding_window();

	void rotate(double new_val);

	std::pair<std::vector<double>&, std::vector<double>&> get_axes();
	std::pair<std::vector<double>&, std::vector<double>&> get_smoothed_axes();

	double latest_smoothed();
	double avg();
	double sum();
	double max();
	double min();
};

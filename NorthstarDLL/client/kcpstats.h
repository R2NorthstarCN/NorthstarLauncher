#pragma once

extern ConVar* Cvar_kcp_stats;

struct sliding_window
{
	std::vector<double> raw;
	std::vector<double> axis_x;

	bool delta = false;
	double last_val = 0;
	double sumed = 0;

	inline sliding_window() : sliding_window(50, false) {};
	sliding_window(size_t samples);
	sliding_window(size_t samples, bool delta);

	void rotate(double new_val);

	std::pair<std::vector<double>&, std::vector<double>&> get_axes();

	double last() const;
	double avg() const;
	double sum() const;
	double max() const;
	double min() const;
};

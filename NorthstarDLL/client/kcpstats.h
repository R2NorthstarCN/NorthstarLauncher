#pragma once

ConVar* Cvar_kcp_stats;
ConVar* Cvar_kcp_stats_interval;

struct sliding_window
{
	std::vector<double> inner;

	sliding_window(size_t samples);

	void rotate(double new_val);
	void rotate_delta(double new_val);

	std::pair<std::vector<double>, std::vector<double>> get_axes();

	double lastest();
	double avg();
	double sum();
	double max();
	double min();
};

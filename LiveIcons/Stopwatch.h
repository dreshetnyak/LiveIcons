#pragma once
#include <chrono>

namespace Utility
{
	using namespace std::chrono;

	class Stopwatch
	{
		time_point<steady_clock> StartTime;

		Stopwatch() : StartTime(high_resolution_clock::now())
		{ }
	public:
		static Stopwatch StartNew() { return Stopwatch{}; }
		template<typename T> [[nodiscard]] long long Elapsed() const;
	};

	template<typename T>
	long long Stopwatch::Elapsed() const
	{
		const auto nowTime = high_resolution_clock::now();
		return time_point_cast<T>(nowTime).time_since_epoch().count() - time_point_cast<T>(StartTime).time_since_epoch().count();
	}
}
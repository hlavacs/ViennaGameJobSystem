#pragma once

namespace vve {


	class VeClock {
		std::chrono::high_resolution_clock::time_point m_last;
		uint32_t m_num_ticks = 0;
		double m_sum_time = 0;
		double m_avg_time = 0;
		double m_stat = 0;
		double f = 1.0;
		std::string m_name;

	public:
		VeClock(std::string name, double stat_time = 1.0) : m_name(name), m_stat(stat_time) {
			m_last = std::chrono::high_resolution_clock::now();
		};

		void start() {
			m_last = std::chrono::high_resolution_clock::now();
		};

		void stop() {
			auto now = std::chrono::high_resolution_clock::now();
			auto time_span = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_last);
			m_sum_time += time_span.count();
			++m_num_ticks;
			if (m_num_ticks >= m_stat) {
				double avg = m_sum_time / (double)m_num_ticks;
				m_avg_time = (1.0 - f) * m_avg_time + f * avg;
				f = f - (f - 0.9) / 100.0;
				m_sum_time = 0;
				m_num_ticks = 0;
				print();
			}
		};

		void tick() {
			auto now = std::chrono::high_resolution_clock::now();
			auto time_span = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_last);
			m_last = now;
			m_sum_time += time_span.count();
			++m_num_ticks;
			if (m_sum_time > m_stat* std::exp(9.0 * std::log(10.0))) {
				double avg = m_sum_time / m_num_ticks;
				m_avg_time = (1.0 - f) * m_avg_time + f * avg;
				f = f - (f - 0.9) / 100.0;
				m_sum_time = 0;
				m_num_ticks = 0;
				print();
			}
			m_last = std::chrono::high_resolution_clock::now();
		};

		void print() {
			std::cout << m_name << " avg " << std::setw(7) << m_avg_time / 1000000.0 << " ms" << std::endl;
		};
	};

};


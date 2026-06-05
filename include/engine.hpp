#pragma once
#include "strategy.hpp"
#include "histogram.hpp"
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <ostream>

namespace csot{
class Engine
{
    public:
    Engine(const std::string& csv_path,Strategy * strategy);
    void load_ticks(const std::string& csv_path);
    void run();
    void print_stats(std::ostream& os) const;

    private:
    std::vector<Tick> _ticks;
    Strategy* strategy;
    LatencyHistogram _hist;
    std::deque<std::string> _symbol_store;
    std::unordered_map<std::string, std::string_view> _symbol_table;
};
}
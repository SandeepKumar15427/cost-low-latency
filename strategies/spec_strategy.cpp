#include "strategy.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

inline constexpr std::size_t  WINDOW        = 64;
inline constexpr double       ENTRY_Z       = 2.0;
inline constexpr double       EXIT_Z        = 0.5;
inline constexpr double       EPSILON_STDDEV = 1e-9;
inline constexpr std::size_t  MAX_SYMBOLS   = 64;

struct SymbolState {
    double          mids[WINDOW]{};   // ring buffer of mid-prices
    std::string_view name{};          // stable string_view from engine intern table
    std::uint32_t   count  = 0;       // ticks seen, capped at WINDOW
    std::uint32_t   head   = 0;       // next write index
    std::int32_t    position = 0;     // -1, 0, or +1
};

class SpecStrategy : public csot::Strategy {
public:
    // on_init: nothing to do — all state is value-initialised above.
    void on_init() override {}

    std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        SymbolState& st = slot(t.symbol);

        // Step 1 — mid price
        const double mid = (t.bid_px + t.ask_px) * 0.5;

        // Step 2 — append to ring buffer
        st.mids[st.head] = mid;
        st.head = (st.head + 1) & 63;   // fast mod because WINDOW == 64
        if (st.count < WINDOW) ++st.count;

        // Step 3 — warm-up guard
        if (st.count < WINDOW) return {};

        // Step 4 — mean and population stddev
        double sum = 0.0;
        for (double x : st.mids) sum += x;
        const double mean = sum * (1.0 / 64.0);

        double sq = 0.0;
        for (double x : st.mids) {
            const double d = x - mean;
            sq += d * d;
        }
        const double stddev = std::sqrt(sq * (1.0 / 64.0));

        if (stddev < EPSILON_STDDEV) return {};

        // Step 5 — z-score
        const double z     = (mid - mean) / stddev;
        const double abs_z = std::fabs(z);

        // Step 6 — entry (only when flat)
        if (st.position == 0) {
            if (z >= ENTRY_Z)
                return {{ csot::Order::Side::SELL, t.symbol, t.bid_px, 1 }};
            if (z <= -ENTRY_Z)
                return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px, 1 }};
            return {};
        }

        // Step 7 — exit (only when holding)
        if (abs_z <= EXIT_Z) {
            if (st.position > 0)
                return {{ csot::Order::Side::SELL, t.symbol, t.bid_px,
                          static_cast<std::uint32_t>(st.position) }};
            if (st.position < 0)
                return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px,
                          static_cast<std::uint32_t>(-st.position) }};
        }

        return {};
    }

    // on_fill — update position; NOT on the latency-critical path.
    void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) override {
        SymbolState& st = slot(o.symbol);
        if (o.side == csot::Order::Side::BUY)
            st.position += static_cast<std::int32_t>(fill_qty);
        else
            st.position -= static_cast<std::int32_t>(fill_qty);
    }

private:
    std::array<SymbolState, MAX_SYMBOLS> states_{};
    std::size_t n_symbols_ = 0;   // how many slots are in use

    SymbolState& slot(std::string_view sym) {
        for (std::size_t i = 0; i < n_symbols_; ++i) {
            if (states_[i].name.data() == sym.data())  // pointer identity first (fast)
                return states_[i];
        }

        for (std::size_t i = 0; i < n_symbols_; ++i) {
            if (states_[i].name == sym)
                return states_[i];
        }
        // New symbol — claim the next free slot.
        SymbolState& s = states_[n_symbols_++];
        s = SymbolState{};   // zero-init
        s.name = sym;
        return s;
    }
};

}  // namespace

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}
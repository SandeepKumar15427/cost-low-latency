#include "strategy.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

inline constexpr std::size_t  WINDOW         = 64;
inline constexpr double       INV_WINDOW     = 1.0 / 64.0;
inline constexpr double       ENTRY_Z        = 2.0;
inline constexpr double       EXIT_Z         = 0.5;
inline constexpr double       EPSILON_STDDEV = 1e-9;
inline constexpr std::size_t  MAX_SYMBOLS    = 64;

// alignas(64) ensures the struct fits perfectly into a CPU cache line
struct alignas(64) SymbolState { 
    double          mids[WINDOW]{};   
    std::string_view name{};          
    std::uint32_t   count  = 0;       
    std::uint32_t   head   = 0;       
    std::int32_t    position = 0;     
};

class SpecStrategy : public csot::Strategy {
public:
    void on_init() override {}

    std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        SymbolState& st = slot(t.symbol);

        // Step 1 — mid price
        const double mid = (t.bid_px + t.ask_px) * 0.5;

        // Step 2 — append to ring buffer (Restored exact sequence)
        st.mids[st.head] = mid;
        st.head = (st.head + 1) & 63;   // fast mod because WINDOW == 64
        if (st.count < WINDOW) [[unlikely]] ++st.count;

        // Step 3 — warm-up guard
        if (st.count < WINDOW) [[unlikely]] return {};

        // Step 4 — mean and population stddev (Restored strict O(N) math)
        double sum = 0.0;
        for (double x : st.mids) sum += x;
        const double mean = sum * INV_WINDOW;

        double sq = 0.0;
        for (double x : st.mids) {
            const double d = x - mean;
            sq += d * d;
        }
        const double stddev = std::sqrt(sq * INV_WINDOW);

        if (stddev < EPSILON_STDDEV) [[unlikely]] return {};

        // Step 5 — z-score (Restored strict division)
        const double z     = (mid - mean) / stddev;
        const double abs_z = std::fabs(z);

        // Step 6 — entry (only when flat)
        if (st.position == 0) [[likely]] {
            if (z >= ENTRY_Z) [[unlikely]]
                return {{ csot::Order::Side::SELL, t.symbol, t.bid_px, 1 }};
            if (z <= -ENTRY_Z) [[unlikely]]
                return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px, 1 }};
            return {};
        }

        // Step 7 — exit (only when holding)
        if (abs_z <= EXIT_Z) [[unlikely]] {
            if (st.position > 0)
                return {{ csot::Order::Side::SELL, t.symbol, t.bid_px,
                          static_cast<std::uint32_t>(st.position) }};
            if (st.position < 0)
                return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px,
                          static_cast<std::uint32_t>(-st.position) }};
        }

        return {};
    }

    void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) override {
        SymbolState& st = slot(o.symbol);
        if (o.side == csot::Order::Side::BUY)
            st.position += static_cast<std::int32_t>(fill_qty);
        else
            st.position -= static_cast<std::int32_t>(fill_qty);
    }

private:
    std::array<SymbolState, MAX_SYMBOLS> states_{};
    std::size_t n_symbols_ = 0;   

    SymbolState& slot(std::string_view sym) {
        // Fast path: pointer identity check directly against the interned string memory
        for (std::size_t i = 0; i < n_symbols_; ++i) {
            if (states_[i].name.data() == sym.data()) [[likely]] 
                return states_[i];
        }

        // Slow path: string equality
        for (std::size_t i = 0; i < n_symbols_; ++i) {
            if (states_[i].name == sym)
                return states_[i];
        }
        
        // Claim new slot
        SymbolState& s = states_[n_symbols_++];
        s = SymbolState{};   
        s.name = sym;
        return s;
    }
};

}  // namespace

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}
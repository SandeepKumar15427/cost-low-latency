#include "strategy.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

inline constexpr std::size_t  WINDOW         = 64;
inline constexpr double       INV_WINDOW     = 1.0 / 64.0;
inline constexpr double       ENTRY_Z_SQ     = 2.0 * 2.0;
inline constexpr double       EXIT_Z_SQ      = 0.5 * 0.5;
inline constexpr double       EPSILON_VAR    = 1e-9 * 1e-9;
inline constexpr std::size_t  MAX_SYMBOLS    = 64;

struct alignas(64) SymbolState { // Cache-line aligned to prevent false sharing in later weeks
    double          mids[WINDOW]{};   
    std::string_view name{};          
    
    // Running sums to eliminate O(N) loops
    double          sum_mids = 0.0;
    double          sum_sq_mids = 0.0;
    
    std::uint32_t   count  = 0;       
    std::uint32_t   head   = 0;       
    std::int32_t    position = 0;     
};

class SpecStrategy : public csot::Strategy {
public:
    void on_init() override {}

    std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        SymbolState& st = slot(t.symbol);

        // Step 1 — Mid price
        const double mid = (t.bid_px + t.ask_px) * 0.5;
        const double mid_sq = mid * mid;

        // Step 2 — O(1) Running sums & ring buffer update
        const double old_mid = st.mids[st.head];
        
        st.sum_mids    = st.sum_mids - old_mid + mid;
        st.sum_sq_mids = st.sum_sq_mids - (old_mid * old_mid) + mid_sq;

        st.mids[st.head] = mid;
        st.head = (st.head + 1) & 63;   // fast mod because WINDOW == 64

        // Step 3 — Warm-up guard (Highly unlikely to be in warmup over millions of ticks)
        if (st.count < WINDOW) [[unlikely]] {
            ++st.count;
            return {};
        }

        // Step 4 — O(1) Mean and Variance
        double sum = 0.0;
        for (double x : st.mids) sum += x;
        const double mean = sum * INV_WINDOW;

        double sq_diff_sum = 0.0;
        for (double x : st.mids) {
            const double d = x - mean;
            sq_diff_sum += d * d;
        }
        const double variance = sq_diff_sum * INV_WINDOW;

        if (variance < EPSILON_VAR) [[unlikely]] return {};

        // Step 5 — Z-Score logic WITHOUT std::sqrt
        const double diff = mid - mean;
        const double sq_diff = diff * diff;

        // Step 6 & 7 — Trading Logic
        if (st.position == 0) [[likely]] {
            // Entry logic
            if (sq_diff >= ENTRY_Z_SQ * variance) [[unlikely]] {
                // Determine direction based on the sign of the difference
                if (diff > 0.0)
                    return {{ csot::Order::Side::SELL, t.symbol, t.bid_px, 1 }};
                else
                    return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px, 1 }};
            }
            return {};
        } else {
            // Exit logic
            if (sq_diff <= EXIT_Z_SQ * variance) [[unlikely]] {
                if (st.position > 0)
                    return {{ csot::Order::Side::SELL, t.symbol, t.bid_px, static_cast<std::uint32_t>(st.position) }};
                else
                    return {{ csot::Order::Side::BUY,  t.symbol, t.ask_px, static_cast<std::uint32_t>(-st.position) }};
            }
            return {};
        }
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
        // Fast path: pointer identity
        for (std::size_t i = 0; i < n_symbols_; ++i) {
            if (states_[i].name.data() == sym.data()) [[likely]] 
                return states_[i];
        }

        // Slow path: string equality (only happens on interned collisions or first sight)
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
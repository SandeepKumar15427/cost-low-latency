#include "engine.hpp"

#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

csot::Engine::Engine(const std::string& csv_path, csot::Strategy* strat)
    : strategy(strat)
{
    load_ticks(csv_path);
}

void csot::Engine::load_ticks(const std::string& csv_path){
    std::ifstream f(csv_path);
    if(!f){ std::cerr<<"csv not opened";return;}
    int i1=0;
    std::string line;
    while(std::getline(f,line)){
        if(i1==0){i1++;continue;}
        // timestamp_ns,symbol,bid_px,ask_px,bid_qty,ask_qty
        // 1700000000000000000,SYM0,99.9900,100.0100,500,500
        csot::Tick t;
        std::string curr;
        int i=0;
        auto process = [&](const std::string& token) {
            switch(i){
                case 0:
                    t.timestamp_ns = std::stoull(token);
                    break;
                case 1:{
                    auto it=_symbol_table.find(token);
                    if(it==_symbol_table.end()){
                        _symbol_store.push_back(token);
                        std::string_view sv = _symbol_store.back();
                        _symbol_table.emplace(_symbol_store.back(), sv);
                        t.symbol = sv;
                    }
                    else t.symbol=it->second;
                    break;
                }
                case 2:  // bid_px  — double
                    t.bid_px = std::stod(token);
                    break;
 
                case 3:  // ask_px  — double
                    t.ask_px = std::stod(token);
                    break;
 
                case 4:  // bid_qty  — uint32_t
                    t.bid_qty = static_cast<uint32_t>(std::stoul(token));
                    break;
 
                case 5:  // ask_qty  — uint32_t
                    t.ask_qty = static_cast<uint32_t>(std::stoul(token));
                    break;
 
                default:
                    break;

            }
            i++;
        };

        for(char c:line){
            if(c==','){
                process(curr);
                curr.clear();
            }
            else curr+=c;
        }
        process(curr);

        _ticks.push_back(t);

    }
}

void csot::Engine::run(){
    if(!strategy){
        std::cerr<<"Strategy not present \n";
        return;
    }
    strategy->on_init();
 
    for (const auto& tick : _ticks) {
        auto t1 = std::chrono::steady_clock::now();
        auto orders = strategy->on_tick(tick);
        auto t2 = std::chrono::steady_clock::now();
 
        _hist.record(
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()
            )
        );
 
        // Feed fills back so position / rolling state stays correct.
        // This is OUTSIDE the timed window — do not move it inside.
        for (const auto& order : orders) {
            // Deterministic fill model from STRATEGY_SPEC.md:
            // BUY  fills at ask_px, SELL fills at bid_px, full qty.
            double fill_price = (order.side == csot::Order::Side::BUY)
                                    ? tick.ask_px
                                    : tick.bid_px;
            strategy->on_fill(order, fill_price, order.qty);
        }
    }
}

void csot::Engine::print_stats(std::ostream& os) const {
    _hist.print(os);
}
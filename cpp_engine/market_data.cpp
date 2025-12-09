#include "include/order_book.hpp"



std::pair<std::pair<double, double>, std::pair<double, double>> 
OrderBook::getBBO() const {

    double bestbidprice = 0.0;
    double bestbidqty = 0.0;
    double bestaskprice = 0.0;
    double bestaskqty = 0.0;

    // Best BID (highest price)
    if (!bids.empty()) {
        const auto& [price, level] = *bids.begin();
        bestbidprice = price;
        bestbidqty   = level.total_qty;
    }

    // Best ASK (lowest price)
    if (!asks.empty()) {
        const auto& [price, level] = *asks.begin();
        bestaskprice = price;
        bestaskqty   = level.total_qty;
    }

    return {{bestbidprice, bestbidqty}, {bestaskprice, bestaskqty}};
}



nlohmann::json OrderBook::getSnapshot(size_t depth) const {

    nlohmann::json snapshot;
    snapshot["symbol"] = symbol;

    // Timestamp (RFC3339 + microseconds)
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto us  = std::chrono::duration_cast<std::chrono::microseconds>(
                  now.time_since_epoch()
              ).count() % 1000000;

    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&t), "%FT%T") << "."
       << std::setw(6) << std::setfill('0') << us << "Z";
    snapshot["timestamp"] = ts.str();

    // -----------------------
    // BBO
    // -----------------------
    auto [bid, ask] = getBBO();
    snapshot["bbo"] = {
        {"bid", {{"price", bid.first}, {"quantity", bid.second}}},
        {"ask", {{"price", ask.first}, {"quantity", ask.second}}}
    };


    //top-depth bids
    nlohmann::json bids_array = nlohmann::json::array();
    size_t count = 0;
    for (const auto& [price, level] : bids) {
        bids_array.push_back({
            {"price", price},
            {"quantity", level.total_qty}
        });
        if (++count >= depth) break;
    }
    snapshot["bids"] = bids_array;

    
    // top-depth asks
    nlohmann::json asks_array = nlohmann::json::array();
    count = 0;
    for (const auto& [price, level] : asks) {
        asks_array.push_back({
            {"price", price},
            {"quantity", level.total_qty}
        });
        if (++count >= depth) break;
    }
    snapshot["asks"] = asks_array;


    return snapshot;
}
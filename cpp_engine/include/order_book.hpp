#pragma once
#include "order.hpp"
#include "trade.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <list>
#include <unordered_map>
#include <atomic>

struct PriceLevel {
    std::list<Order> orders;
    double total_qty = 0.0;
};

struct OrderLocator {
    Side side;
    double price;
    std::list<Order>::iterator it;
};

class OrderBook {
private:
    std::string symbol;

    std::map<double, PriceLevel, std::greater<double>> bids;
    std::map<double, PriceLevel, std::less<double>> asks;

    std::unordered_map<uint64_t, OrderLocator> locators;

    std::vector<Trade> trades;
    std::mutex mtx;
    static std::atomic<uint64_t> order_id_counter;

    double maker_fee_rate = 0.001;
    double taker_fee_rate = 0.002;

public:
    OrderBook(const std::string& symbol);

    std::vector<Trade> addOrder(double price, double quantity, std::string side, std::string type);
    std::vector<Trade> limitOrder(Order& order);
    std::vector<Trade> marketOrder(Order& order);
    std::vector<Trade> iocOrder(Order& order);
    std::vector<Trade> fokOrder(Order& order);

    void updateLevel(Side side,double price,double qtychange);

    std::pair<std::pair<double, double>, std::pair<double, double>> getBBO() const;
    nlohmann::json getSnapshot(size_t depth) const;

    std::function<void(const Trade&)> trade_callback;
    double calculateFee(Side side, double amount) const;
};

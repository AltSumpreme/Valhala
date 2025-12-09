#include "include/order_book.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <functional>
#include <list>




static std::atomic<uint64_t> trade_id_counter{1}; // Atomic counter for trade IDs
std::atomic<uint64_t> OrderBook::order_id_counter{1}; // Atomic counter for order IDs

OrderBook::OrderBook(const std::string& sym) : symbol(sym) {}



std::vector<Trade> OrderBook::addOrder(double price, double quantity, std::string side_str, std::string type_str){
    std::lock_guard<std::mutex> lock(mtx);

    std::vector<Trade> executed_trades;
    Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
    OrderType type = (type_str == "LIMIT") ? OrderType::LIMIT : OrderType::MARKET;

    Order order(order_id_counter.fetch_add(1, std::memory_order_relaxed), side, type, price, quantity);

    if(type == OrderType::MARKET){
       executed_trades = marketOrder(order);
    }
    else if(type == OrderType::IOC){
       executed_trades = iocOrder(order);
    }
    else if(type == OrderType::FOK){
       executed_trades = fokOrder(order);
    }
    else {
        // Limit order
        if (order.side == Side::BUY) {
            bids[price].orders.push_back(order);
            auto it  = std::prev(bids[price].orders.end());
            locators[order.order_id] = OrderLocator{Side::BUY,price,it};
            bids[price].total_qty +=quantity;
          
        } else {
            asks[price].orders.push_back(order);
            auto it = std::prev(asks[price].orders.end());
            locators[order.order_id] = OrderLocator{Side::SELL, price, it};
            asks[price].total_qty += quantity;

        }
        executed_trades = limitOrder(order);
        

       
    }
      return executed_trades;
     
}



//Matching engine

std::vector<Trade> OrderBook::limitOrder(Order& order) {
    std::vector<Trade> newTrades;

    while (!bids.empty() && !asks.empty()) {
        auto best_bid_it = bids.begin();
        auto best_ask_it = asks.begin();

        PriceLevel& bid_level = best_bid_it->second;
        PriceLevel& ask_level = best_ask_it->second;

        Order& top_bid = bid_level.orders.front();
        Order& top_ask = ask_level.orders.front();

        if (top_bid.price >= top_ask.price) {
            double trade_price = top_ask.price;
            double trade_quantity = std::min(top_bid.quantity, top_ask.quantity);

            Trade trade;
            trade.trade_id = trade_id_counter.fetch_add(1, std::memory_order_relaxed);
            trade.symbol = symbol;
            trade.price = trade_price;
            trade.quantity = trade_quantity;
            trade.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            trade.maker_order_id = top_ask.order_id;
            trade.taker_order_id = top_bid.order_id;
            trade.aggressor_side = (order.side == Side::BUY) ? Side::BUY : Side::SELL;

            if (order.side == Side::BUY) {
                trade.maker_fee = calculateFee(Side::SELL, trade_quantity * trade_price);
                trade.taker_fee = calculateFee(Side::BUY, trade_quantity * trade_price);
            } else {
                trade.maker_fee = calculateFee(Side::BUY, trade_quantity * trade_price);
                trade.taker_fee = calculateFee(Side::SELL, trade_quantity * trade_price);
            }

            newTrades.push_back(trade);
            trades.push_back(trade);

            if (trade_callback) {
                trade_callback(trade);
            }

            top_bid.quantity -= trade_quantity;
            top_ask.quantity -= trade_quantity;

            if (top_bid.quantity <= 0) {
                locators.erase(top_bid.order_id);
                bid_level.orders.pop_front();
                if (bid_level.orders.empty()) {
                    bids.erase(best_bid_it);
                }
            }

            if (top_ask.quantity <= 0) {
                locators.erase(top_ask.order_id);
                ask_level.orders.pop_front();
                if (ask_level.orders.empty()) {
                    asks.erase(best_ask_it);
                }
            }
        } else {
            break;
        }
    }

    return newTrades;
}


std::vector<Trade> OrderBook::marketOrder(Order& order) {
    std::vector<Trade> trades_executed;

    if (order.side == Side::BUY) {
        while (order.quantity > 0 && !asks.empty()) {
            auto best_ask_it = asks.begin();
            PriceLevel& ask_level = best_ask_it->second;
            Order& top_ask = ask_level.orders.front();

            double trade_quantity = std::min(order.quantity, top_ask.quantity);
            double trade_price = top_ask.price;

            Trade trade;
            trade.trade_id = trade_id_counter.fetch_add(1, std::memory_order_relaxed);
            trade.symbol = symbol;
            trade.price = trade_price;
            trade.quantity = trade_quantity;
            trade.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            trade.maker_order_id = top_ask.order_id;
            trade.taker_order_id = order.order_id;
            trade.aggressor_side = Side::BUY;
            trade.maker_fee = calculateFee(Side::SELL, trade_quantity * trade_price);
            trade.taker_fee = calculateFee(Side::BUY, trade_quantity * trade_price);

            trades.push_back(trade);
            trades_executed.push_back(trade);
            if (trade_callback) trade_callback(trade);

            order.quantity -= trade_quantity;
            top_ask.quantity -= trade_quantity;

            

            if (top_ask.quantity <= 0) {
                locators.erase(top_ask.order_id);
                ask_level.orders.pop_front();
                if (ask_level.orders.empty()) {
                    asks.erase(best_ask_it);
                }
            }
        }
    } else {
        while (order.quantity > 0 && !bids.empty()) {
            auto best_bid_it = bids.begin();
            PriceLevel& bid_level = best_bid_it->second;
            Order& top_bid = bid_level.orders.front();

            double trade_quantity = std::min(order.quantity, top_bid.quantity);
            double trade_price = top_bid.price;

            Trade trade;
            trade.trade_id = trade_id_counter.fetch_add(1, std::memory_order_relaxed);
            trade.symbol = symbol;
            trade.price = trade_price;
            trade.quantity = trade_quantity;
            trade.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            trade.maker_order_id = top_bid.order_id;
            trade.taker_order_id = order.order_id;
            trade.aggressor_side = Side::SELL;
            trade.maker_fee = calculateFee(Side::BUY, trade_quantity * trade_price);
            trade.taker_fee = calculateFee(Side::SELL, trade_quantity * trade_price);

            trades.push_back(trade);
            trades_executed.push_back(trade);
            if (trade_callback) trade_callback(trade);

            order.quantity -= trade_quantity;
            top_bid.quantity -= trade_quantity;

           

            if (top_bid.quantity <= 0) {
                locators.erase(top_bid.order_id);
                bid_level.orders.pop_front();
                if (bid_level.orders.empty()) {
                    bids.erase(best_bid_it);
                }
            }
        }
    }

    return trades_executed;
}



// Update order book levels
void OrderBook::updateLevel(Side side, double price, double qtychange) {
    if (side == Side::BUY) {
        auto it = bids.find(price);
        if (it == bids.end()) {
            if (qtychange > 0) {
                bids[price].total_qty = qtychange;
            }
            return;
        }
        it->second.total_qty += qtychange;
        if (it->second.total_qty <= 0.0) {
            bids.erase(it);
        }
    } else {
        auto it = asks.find(price);
        if (it == asks.end()) {
            if (qtychange > 0) {
                asks[price].total_qty = qtychange;
            }
            return;
        }
        it->second.total_qty += qtychange;
        if (it->second.total_qty <= 0.0) {
            asks.erase(it);
        }
    }
}


std::vector<Trade> OrderBook::iocOrder(Order& order) {
    std::vector<Trade> trades_executed;

    if (order.side == Side::BUY) {
        while (order.quantity > 0 && !asks.empty()) {
            auto it = asks.begin();
            PriceLevel& level = it->second;
            Order& top = level.orders.front();
            if (top.price > order.price) break;

            double q = std::min(order.quantity, top.quantity);
            double p = top.price;

            Trade trade;
            trade.trade_id = trade_id_counter.fetch_add(1, std::memory_order_relaxed);
            trade.symbol = symbol;
            trade.price = p;
            trade.quantity = q;
            trade.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            trade.maker_order_id = top.order_id;
            trade.taker_order_id = order.order_id;
            trade.aggressor_side = Side::BUY;

            trades.push_back(trade);
            trades_executed.push_back(trade);

            order.quantity -= q;
            top.quantity -= q;

           

            if (top.quantity <= 0) {
                locators.erase(top.order_id);
                level.orders.pop_front();
                if (level.orders.empty()) asks.erase(it);
            }
        }
    } else {
        while (order.quantity > 0 && !bids.empty()) {
            auto it = bids.begin();
            PriceLevel& level = it->second;
            Order& top = level.orders.front();
            if (top.price < order.price) break;

            double q = std::min(order.quantity, top.quantity);
            double p = top.price;

            Trade trade;
            trade.trade_id = trade_id_counter.fetch_add(1, std::memory_order_relaxed);
            trade.symbol = symbol;
            trade.price = p;
            trade.quantity = q;
            trade.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            trade.maker_order_id = top.order_id;
            trade.taker_order_id = order.order_id;
            trade.aggressor_side = Side::SELL;

            trades.push_back(trade);
            trades_executed.push_back(trade);

            order.quantity -= q;
            top.quantity -= q;

            
            if (top.quantity <= 0) {
                locators.erase(top.order_id);
                level.orders.pop_front();
                if (level.orders.empty()) bids.erase(it);
            }
        }
    }

    return trades_executed;
}


std::vector<Trade> OrderBook::fokOrder(Order& order) {
    std::vector<Trade> trades_executed;
    double available = 0.0;

    if (order.side == Side::BUY) {
        for (auto& [price, level] : asks) {
            if (price > order.price) break;
            available += level.total_qty;
            if (available >= order.quantity) break;
        }
        if (available < order.quantity) return trades_executed;
        trades_executed = iocOrder(order);
    } else {
        for (auto& [price, level] : bids) {
            if (price < order.price) break;
            available += level.total_qty;
            if (available >= order.quantity) break;
        }
        if (available < order.quantity) return trades_executed;
        trades_executed = iocOrder(order);
    }

    return trades_executed;
}


// helper function to calculate maker-taker fees

double OrderBook::calculateFee(Side side, double amount) const {
    if (side == Side::BUY) {
        return amount * taker_fee_rate; // Assuming BUY orders are taker orders
    } else {
        return amount * maker_fee_rate; // Assuming SELL orders are maker orders
    }
}
#pragma once

#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <condition_variable>

#include "Aliases.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Trade.h"

class Orderbook
{
public:

	Orderbook();
	
	Orderbook(const Orderbook&) = delete;
	void operator=(const Orderbook&) = delete;
	
	Orderbook(Orderbook&&) = delete;
	void operator=(Orderbook&&) = delete;
	
	~Orderbook();

	std::size_t Size() const;
	void CancelOrder(OrderId orderId);
	Trades AddOrder(OrderPointer order);
	Trades ModifyOrder(OrderModify order);
	OrderbookLevelInfos GetOrderInfos() const;

private:

	struct OrderEntry
	{
		OrderPointer order_{ nullptr };
		OrderPointers::iterator location_;
	};
	
	struct LevelData
	{
		Quantity quantity_{};
		Quantity count_{};

		enum class Action
		{
			Add,
			Remove,
			Match,
		};
	};

	std::unordered_map<Price, LevelData> data_;
	std::unordered_map<OrderId, OrderEntry> orders_;
	std::map<Price, OrderPointers, std::less<Price>> asks_;
	std::map<Price, OrderPointers, std::greater<Price>> bids_;
	
	std::jthread ordersRemoveThread_;
	mutable std::mutex ordersMutex_;
	std::atomic<bool> shutdown_{ false };
	std::condition_variable shutdownConditionVariable_;

	void RemoveGoodForDayOrders();

	void CancelOrders(OrderIds const& orderIds);
	void CancelOrderInternal(OrderId orderId);
	
	void OnOrderAdded(OrderPointer order);
	void OnOrderCancelled(OrderPointer order);
	void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
	
	void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

	bool CanFullyFill(Side side, Price price, Quantity quantity) const;
	bool CanMatch(Side side, Price price) const;
	Trades MatchOrders();
};
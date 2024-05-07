#pragma once

#include <map>
#include <unordered_map>

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

	Trades AddOrder(OrderPointer order);
	void CancelOrder(OrderId orderId);
	Trades MatchOrder(OrderModify order);
	
	std::size_t Size() const;
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

	std::map<Price, OrderPointers, std::greater<Price>> bids_;
	std::map<Price, OrderPointers, std::less<Price>> asks_;
	std::unordered_map<OrderId, OrderEntry> orders_;

	bool CanMatch(Side side, Price price) const
	{
		if (side == Side::Buy)
		{
			if (asks_.empty())
			{
				return false;
			}

			const auto& [bestAsk, _] = *asks_.begin();
			return price >= bestAsk;
		}
		else
		{
			if (bids_.empty())
			{
				return false;
			}

			const auto& [bestBid, _] = *bids_.begin();
			return price <= bestBid;
		}
	}

	Trades MatchOrders()
	{
		Trades trades;
		trades.reserve(orders_.size());

		while (true)
		{
			if (bids_.empty() || asks_.empty())
			{
				break;
			}

			auto& [bidPrice, bids] = *bids_.begin();
			auto& [askPrice, asks] = *asks_.begin();

			if (bidPrice < askPrice) { break; }

			while (bids.size() && asks.size())
			{
				auto const& bid = bids.front();
				auto const& ask = asks.front();

				Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

				bid->Fill(quantity);
				ask->Fill(quantity);

				if (bid->IsFilled())
				{
					bids.pop_front();
					orders_.erase(bid->GetOrderId());
				}

				if (ask->IsFilled())
				{
					asks.pop_front();
					orders_.erase(ask->GetOrderId());
				}

				if (bids.empty())
				{
					bids_.erase(bidPrice);
				}

				if (asks.empty())
				{
					asks_.erase(askPrice);
				}

				trades.emplace_back(TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
					TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity });
			}
		}

		if (!bids_.empty())
		{
			auto& [_, bids] = *bids_.begin();
			auto const& order = bids.front();
			if (order->GetOrderType() == OrderType::FillAndKill)
			{
				CancelOrder(order->GetOrderId());
			}
		}

		if (!asks_.empty())
		{
			auto& [_, asks] = *asks_.begin();
			auto const& order = asks.front();
			if (order->GetOrderType() == OrderType::FillAndKill)
			{
				CancelOrder(order->GetOrderId());
			}
		}

		return trades;
	}
};
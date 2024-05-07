#include <cstdint>
#include <vector>
#include <stdexcept>
#include <format>
#include <memory>
#include <list>
#include <map>
#include <numeric>
#include <unordered_map>
#include <iostream>

// GTC, F&K
enum class OrderType
{
	GoodTillCancel,
	FillAndKill
};

// Buy, Sell
enum class Side
{
	Buy,
	Sell
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

// Note(vss): struct with Price and Quantity.
struct LevelInfo 
{
	Price price_;
	Quantity quantity_;
};

// Note(vss): defines a vector of LevelInfo struct, each storing price and quantity information.
using LevelInfos = std::vector<LevelInfo>;

/**
* @brief Encapsulate LevelInfos objects to represent our sides. Each side is a list of levels.
*/
class OrderbookLevelInfos
{
public:

	OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks) :
		bids_{ bids },
		asks_{ asks } 
	{}

	const LevelInfos& GetBids() const { return bids_; }
	const LevelInfos& GetAsks() const { return asks_; }

private:

	LevelInfos bids_;
	LevelInfos asks_;
};

/**
* @brief Order objects will contain properties such as orderType, orderId, side, price and quantity.
* The class provides APIs for actions like filling the order, getting the filled quantity,
* and checking if the order is filled, etc.
*/
class Order
{
public:

	Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity) :
		orderType_{ orderType },
		orderId_{ orderId },
		side_{ side },
		price_{ price },
		initialQuantity_{ quantity },
		remainingQuantity_{ quantity }
	{}

	OrderId GetOrderId() const { return orderId_; }
	Side GetSide() const { return side_; }
	Price GetPrice() const { return price_; }
	OrderType GetOrderType() const { return orderType_; }
	Quantity GetInitialQuantity() const { return initialQuantity_; }
	Quantity GetRemainingQuantity() const { return remainingQuantity_; }
	Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
	bool IsFilled() const { return GetRemainingQuantity() == 0; }
	void Fill(Quantity quantity)
	{
		if (quantity > GetRemainingQuantity())
		{
			throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));
		}
		remainingQuantity_ -= quantity;
	}

private:

	OrderType orderType_;
	OrderId orderId_;
	Side side_;
	Price price_;
	Quantity initialQuantity_;
	Quantity remainingQuantity_;
};

// Note(vss): can be stored in a orders dictionary and a bid/ask dictionary
using OrderPointer = std::shared_ptr<Order>;
// TODO(vss): use std::vector
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
public:

	OrderModify(OrderId orderId, Side side, Price price, Quantity quantity) :
		orderId_{ orderId },
		side_{ side },
		price_{ price },
		quantity_{ quantity }
	{}

	OrderId GetOrderId() const { return orderId_; }
	Side GetSide() const { return side_; }
	Price GetPrice() const { return price_; }
	Quantity GetQuantity() const { return quantity_; }

	OrderPointer ToOrderPointer(OrderType type) const
	{
		return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
	}

private:

	OrderId orderId_;
	Side side_;
	Price price_;
	Quantity quantity_;
};

struct TradeInfo
{
	OrderId orderId_;
	Price price_;
	Quantity quantity_;
};

/** 
* @brief A Trade object is an aggregation of two TradeInfo objects, the bid side and the ask side trade.
* There is a TradeInfo object for the bid, and one for the ask, because a bid has to match an ask and a ask has to match a bid.
* Every TradeInfo object will have the OrderId of what's been traded, the price it traded at and the quantity
*/
class Trade
{
public:

	Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade) :
		bidTrade_{ bidTrade },
		askTrade_{ askTrade }
	{}

	const TradeInfo& GetBidTrade() const { return bidTrade_; }
	const TradeInfo& GetAskTrade() const { return askTrade_; }

private:

	TradeInfo bidTrade_;
	TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class Orderbook
{
public:

	Trades AddOrder(OrderPointer order)
	{
		if (orders_.contains(order->GetOrderId())) 
		{ 
			return { }; 
		}

		if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
		{
			return { };
		}

		OrderPointers::iterator	iterator;

		if (order->GetSide() == Side::Buy)
		{
			auto& orders = bids_[order->GetPrice()];
			orders.push_back(order);
			iterator = std::next(orders.begin(), orders.size() - 1);
		}
		else
		{
			auto& orders = asks_[order->GetPrice()];
			orders.push_back(order);
			iterator = std::next(orders.begin(), orders.size() - 1);
		}

		orders_.try_emplace(order->GetOrderId(),  order, iterator);
		return MatchOrders();
	}

	void CancelOrder(OrderId orderId)
	{
		if (!orders_.contains(orderId)) 
		{ 
			return; 
		}

		const auto [order, iterator] = orders_.at(orderId);
		orders_.erase(orderId);

		if (order->GetSide() == Side::Sell)
		{
			auto price = order->GetPrice();
			auto& orders = asks_.at(price);
			orders.erase(iterator);
			if (orders.empty())
			{
				asks_.erase(price);
			}
		}
		else
		{
			auto price = order->GetPrice();
			auto& orders = bids_.at(price);
			orders.erase(iterator);
			if (orders.empty())
			{
				bids_.erase(price);
			}
		}
	}

	Trades MatchOrder(OrderModify order)
	{
		if (!orders_.contains(order.GetOrderId()))
		{
			return { };
		}

		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
		CancelOrder(order.GetOrderId());
		return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
	}

	std::size_t Size() const { return orders_.size(); }

	OrderbookLevelInfos GetOrderInfos() const
	{
		LevelInfos bidInfos;
		LevelInfos askInfos;
		bidInfos.reserve(orders_.size());
		askInfos.reserve(orders_.size());

		auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
		{
			return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
				[](Quantity runningSum, const OrderPointer& order)
				{ return runningSum + order->GetRemainingQuantity(); }) };
		};

		for (const auto& [price, orders] : bids_)
		{
			bidInfos.push_back(CreateLevelInfos(price, orders));
		}

		for (const auto& [price, orders] : asks_)
		{
			askInfos.push_back(CreateLevelInfos(price, orders));
		}

		return OrderbookLevelInfos{ bidInfos, askInfos };
	}

private:

	struct OrderEntry
	{
		OrderPointer order_{ nullptr };
		OrderPointers::iterator location_;
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

				trades.emplace_back(TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity},
									TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity});
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

int main()
{
	Orderbook orderbook;
	const OrderId orderId = 1;
	orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
	std::cout << orderbook.Size() << std::endl; // should be 1
	orderbook.CancelOrder(orderId);
	std::cout << orderbook.Size() << std::endl; // should be 0

	return 0;
}
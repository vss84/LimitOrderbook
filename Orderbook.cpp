#include "Orderbook.h"

#include <ctime>
#include <chrono>
#include <numeric>

void Orderbook::RemoveGoodForDayOrders()
{
	using namespace std;
	const auto end = chrono::hours(16);

	while (true)
	{
		const auto now = chrono::system_clock::now();
		const auto now_c = chrono::system_clock::to_time_t(now);
		std::tm now_parts = { };
		localtime_s(&now_parts, &now_c);

		if (now_parts.tm_hour >= end.count())
		{
			now_parts.tm_mday += 1;
		}

		now_parts.tm_hour = end.count();
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;

		auto next = chrono::system_clock::from_time_t(mktime(&now_parts));
		auto till = next - now + chrono::milliseconds(100);

		{
			std::unique_lock ordersLock{ ordersMutex_ };

			if (shutdown_.load(std::memory_order_acquire) ||
				shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
			{
				return;
			}
		}

		OrderIds orderIds;

		{
			std::scoped_lock orderLock{ ordersMutex_ };

			for (const auto& [_, entry] : orders_)
			{
				const auto& [order, _] = entry;

				if (order->GetOrderType() != OrderType::GoodForDay) 
				{ 
					continue;
				}

				orderIds.push_back(order->GetOrderId());
			}
		}

		CancelOrders(orderIds);
	}
}

void Orderbook::CancelOrders(OrderIds const& orderIds)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	for (const auto& orderId : orderIds)
	{
		CancelOrderInternal(orderId);
	}
}

void Orderbook::CancelOrderInternal(OrderId orderId)
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

	OnOrderCancelled(order);
}

void Orderbook::OnOrderCancelled(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
	UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
	auto& data = data_[price];

	if (action == LevelData::Action::Remove) 
	{ 
		data.count_ -= 1; 
	}
	else if (action == LevelData::Action::Add)
	{
		data.count_ += 1;
	}

	if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
	{
		data.quantity_ -= quantity;
	}
	else
	{
		data.quantity_ += quantity;
	}
	
	if (data.count_ == 0)
	{
		data_.erase(price);
	}
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
	{
		return false;
	}

	std::optional<Price> threshold;

	if (side == Side::Buy)
	{
		const auto& [askPrice, _] = *asks_.begin();
		threshold = askPrice;
	}
	else
	{
		const auto& [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
	}

	for (const auto& [levelPrice, levelData] : data_)
	{
		using enum Side;

		if (threshold.has_value() &&
			(side == Buy && threshold.value() > levelPrice) ||
			(side == Sell && threshold.value() < levelPrice))
		{
			continue;
		}

		if ((side == Buy && levelPrice > price) ||
			(side == Sell && levelPrice < price))
		{
			continue;
		}

		if (quantity <= levelData.quantity_)
		{
			return true;
		}

		quantity -= levelData.quantity_;
	}

	return false;
}

Trades Orderbook::AddOrder(OrderPointer order)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	if (orders_.contains(order->GetOrderId()))
	{
		return { };
	}

	if (order->GetOrderType() == OrderType::Market)
	{
		if (order->GetSide() == Side::Buy && !asks_.empty())
		{
			const auto& [worstAsk, _] = *asks_.rbegin();
			order->ToGoodTillCancel(worstAsk);
		}
		else if (order->GetSide() == Side::Sell && !bids_.empty())
		{
			const auto& [worstBid, _] = *bids_.rbegin();
			order->ToGoodTillCancel(worstBid);
		}
		else
		{
			return { };
		}
	}

	if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
	{
		return { };
	}
	if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
	{
		return { };
	}

	OrderPointers::iterator	iterator;

	if (order->GetSide() == Side::Buy)
	{
		auto& orders = bids_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}
	else
	{
		auto& orders = asks_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}

	orders_.try_emplace(order->GetOrderId(), order, iterator);

	OnOrderAdded(order);

	return MatchOrders();
}

Orderbook::Orderbook() : ordersRemoveThread_{ [this] { RemoveGoodForDayOrders(); } } {}

Orderbook::~Orderbook()
{
	shutdown_.store(true, std::memory_order_release);
	shutdownConditionVariable_.notify_one();
	ordersRemoveThread_.join();
}

void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	CancelOrderInternal(orderId);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
	OrderType orderType;

	{
		std::scoped_lock ordersLock{ ordersMutex_ };

		if (!orders_.contains(order.GetOrderId()))
		{
			return { };
		}

		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
		orderType = existingOrder->GetOrderType();
	}

	CancelOrder(order.GetOrderId());

	return AddOrder(order.ToOrderPointer(orderType));
}

std::size_t Orderbook::Size() const
{ 
	std::scoped_lock ordersLocks{ ordersMutex_ };
	return orders_.size(); 
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const
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

bool Orderbook::CanMatch(Side side, Price price) const
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

Trades Orderbook::MatchOrders()
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

		if (bidPrice < askPrice) 
		{ 
			break; 
		}

		while (bids.size() && asks.size())
		{
			auto bid = bids.front();
			auto ask = asks.front();

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

			trades.emplace_back(TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
								TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity });

			OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
			OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
		}
		
		if (bids.empty())
		{
			bids_.erase(bidPrice);
		}

		if (asks.empty())
		{
			asks_.erase(askPrice);
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

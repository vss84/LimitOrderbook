#pragma once

#include "TradeInfo.h"

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
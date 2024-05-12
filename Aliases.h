#pragma once

#include <vector>
#include <limits>


using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using OrderIds = std::vector<OrderId>;

struct Constants
{
	static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};
#pragma once

// GTC, F&K
enum class OrderType
{
	GoodTillCancel,
	FillAndKill,
	FillOrKill,
	GoodForDay,
};
#pragma once

#include "Aliases.h"

// Note(vss): struct with Price and Quantity.
struct LevelInfo
{
	Price price_;
	Quantity quantity_;
};

// Note(vss): defines a vector of LevelInfo struct, each storing price and quantity information.
using LevelInfos = std::vector<LevelInfo>;
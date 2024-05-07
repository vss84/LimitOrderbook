#include <iostream>
#include "Orderbook.h"
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
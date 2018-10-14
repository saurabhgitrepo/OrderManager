#ifndef ORDERMANAGER_H
#define ORDERMANAGER_H

#include <iostream>
#include <memory>	// for shared_ptr
#include <unordered_map>
#include "OrderListnerInterface.h"

enum class OrderState { NewPending, Active, Rejected, ReplacePending, PartiallyFilled, Completed };

class Order
{
	int id;
	char side;
	double price;
	int totalQuantity;	// filled + remaining
public:
	int remainingQuantity;
	int filledQuantity;
	OrderState orderState;

	Order(int id, char side, double price, int quantity) : id(id), side(side), price(price), totalQuantity(quantity), remainingQuantity(quantity), filledQuantity(0), orderState(OrderState::NewPending) {}
	char Side() const { return side; }
	double Price() const { return price; }
	void ChangeOrderState(bool isPendingOrderUpdate = false);
	void replaceOrder(int newId, int deltaQuantity);
};


class OrderManager : public Listener
{
	int nfq = 0;
	long double cov[2] = { 0.0, 0.0 };
	long double pov_min[2] = { 0.0, 0.0 };
	long double pov_max[2] = { 0.0, 0.0 };

	std::unordered_map<int, std::pair<int, int>> replacePendingOrdersMap;
	std::unordered_map<int, std::shared_ptr<Order>> orders;

	void updateNFQ(char side, int quantityFilled);
	void updateCOV(char side, long double value);
	void updatePOV(char side, long double minValue, long double maxValue);

public:
	/* Description - Indicates the Net Filled Quantity (NFQ) for all orders.
	*/
	int getNFQ() { return nfq; }

	/* Description - Indicates the Confirmed Order Value (COV) for all orders of given side.
	     COV = New Acknowledged order + Remaining quantity of partial filled orders
	*/
	long double getCOV(char side) { return cov[side == 'B']; }

	/* Description - Indicates the Pending Order Value (COV) for all orders of given side.
		 POV_min = New Acknowledged order + Remaining quantity of partial filled orders
	*/
	long double getPOV_min(char side) { return pov_min[side == 'B']; }
	long double getPOV_max(char side) { return pov_max[side == 'B']; }

	/* Description - Indicates the client has sent a new order request to the market.
	*/
	virtual void OnInsertOrderRequest(int id, char side, double price, int quantity) override;

	/* Description - Indicates the client has sent a request to change the quantity of an order.
	   Assumption -
	     1. deltaQuantity will be positive when increase in quantity
	     2. deltaQuantity will be negative when decrease in quantity
	*/
	virtual void OnReplaceOrderRequest(int oldId, int newId, int deltaQuantity) override;

	/* Description - Indicates the insert or modify request was accepted.
	   Assumptions -
	     1. In case on OnReplaceOrderRequest, id => oldId
	*/
	virtual void OnRequestAcknowledged(int id) override;

	/* Description - Indicates the insert or modify request was rejected.
	   Assumption -
	     1. In case of Replace request rejected, the Id is oldId
	*/
	virtual void OnRequestRejected(int id) override;

	/* Description - Indicates that the order quantity was reduced (and filled) by quantityFilled.
	   Assumtions - 
	     1. Processing fills even when order is in pending state (NewPending or ReplacePending).
	     2. Allowing Fills more than total quantity (to support additional increased delta quantity of pending replace request) 
	     3. All fills are recieved as per oldId until pending Replace request is acknowwledged
	*/
	virtual void OnOrderFilled(int id, int quantityFilled) override;
};

#endif // !ORDERMANAGER_H

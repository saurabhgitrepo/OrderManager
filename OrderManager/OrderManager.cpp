#include <iostream>
#include <memory>	// for shared_ptr
#include <unordered_map>

using namespace std;

class Listener
{
public:
// These two callbacks represent client requests.	

	// Indicates the client has sent a new order request to the market.
	// Exactly one callback will follow:
	// OnRequestAcknowledged, in which case order is active in the market;
	// or
	// OnRequestRejected, in which case the order was never active in the market.
	virtual void OnInsertOrderRequest(int id, char side /* B for bid, O for offer */, double price, int quantity) = 0;

	// Indicates the client has sent a request to change the quantity of an order.
	// Exactly one callback will follow:
	// OnRequestAcknowledged, in which case the order quantity was modified and the order is now tracked by ID newId; or
	// OnRequestRejected, in which case the order was not modified and remains tracked by ID oldId.
	virtual void OnReplaceOrderRequest(int oldId /* The existing order to modify*/, int newId /* The new order ID to use if the modification succeeds */, int deltaQuantity) = 0; // How much the quantity should be increased/decreased

// These three callbacks represent market confirmations.

	// Indicates the insert or modify request was accepted.
	virtual void OnRequestAcknowledged(int id) = 0;

	// Indicates the insert or modify request was rejected.
	virtual void OnRequestRejected(int id) = 0;

	// Indicates that the order quantity was reduced (and filled) by quantityFilled.
	virtual void OnOrderFilled(int id, int quantityFilled) = 0;
};

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
public:
	Order(int id, char side, double price, int quantity) : id(id), side(side), price(price), totalQuantity(quantity), remainingQuantity(quantity), filledQuantity(0), orderState(OrderState::NewPending) {}
	char Side() const { return side; }
	double Price() const { return price; }
	void replaceOrder(int newId, int deltaQuantity)
	{
		id = newId;
		totalQuantity += deltaQuantity;
		remainingQuantity += deltaQuantity;
	}

	void ChangeOrderState(bool isPendingOrderUpdate = false)
	{
		if (isPendingOrderUpdate || orderState != OrderState::NewPending && orderState != OrderState::ReplacePending && orderState != OrderState::Rejected)
		{
			// Changing Order State based on filled quantity and remaining quantity 
			if (filledQuantity == 0)
			{
				orderState = OrderState::Active;
			}
			else if (filledQuantity > 0 && remainingQuantity > 0)
			{
				orderState = OrderState::PartiallyFilled;
			}
			else //	remainingQuantity <= 0
			{
				orderState = OrderState::Completed;
			}
		}
	}
};

class OrderManager : public Listener
{
	int nfq = 0;
	long double cov[2] = { 0.0, 0.0 };
	long double pov[2] = { 0.0, 0.0 };

	unordered_map<int, pair<int, int>> replacePendingOrdersMap;
	unordered_map<int, shared_ptr<Order>> orders;

private:
	void updateNFQ(char side, int quantityFilled)
	{
		if (side == 'B')
			nfq += quantityFilled;
		else
			nfq -= quantityFilled;
	}

	// COV	-	New Acknowledged order + Remaining quantity of partial filled orders
	void updateCOV(char side, long double value)
	{
		cov[side == 'B'] += value;
	}

	// POV	-	
	void updatePOV(char side, long double value)
	{
		pov[side == 'B'] += value;
	}
public:
	virtual void OnInsertOrderRequest(int id, char side, double price, int quantity) override
	{
		auto it = orders.find(id);
		if (it == orders.end())
		{
			orders.insert(make_pair(id, shared_ptr<Order>(new Order(id, side, price, quantity))));
		}
		else
		{
			// duplicate order id, log error
		}
	}

	// Assumption -
	// 1. deltaQuantity will be positive when increase in quantity
	// 2. deltaQuantity will be negative when decrease in quantity
	virtual void OnReplaceOrderRequest(int oldId, int newId, int deltaQuantity) override
	{
		auto it = orders.find(oldId);
		if (it != orders.end())
		{
			shared_ptr<Order> orderPtr = it->second;

			if (orderPtr->orderState != OrderState::NewPending && orderPtr->orderState != OrderState::ReplacePending)
			{
				replacePendingOrdersMap[oldId] = make_pair(newId, deltaQuantity);

				orderPtr->orderState = OrderState::ReplacePending;

				updateCOV(orderPtr->Side(), -(orderPtr->remainingQuantity * orderPtr->Price()));
			}
			else
			{
				// already pending request (NewPending or ReplacePending), log error
			}
		}
		else
		{
			// Order not present, log error
		}
	}

	virtual void OnRequestAcknowledged(int id) override
	{
		auto it = orders.find(id);
		if (it != orders.end())
		{
			shared_ptr<Order> orderPtr = it->second;

			if (orderPtr->orderState == OrderState::NewPending)
			{
				orderPtr->ChangeOrderState(true);	// ChangeOrderState(isPendingUpdate = true)

				updateCOV(orderPtr->Side(), orderPtr->Price() * orderPtr->remainingQuantity);
			}
			else if (orderPtr->orderState == OrderState::ReplacePending) // TODO, add other Pending States ack, for ex ReplacePending
			{
				auto newId_deltaQty = replacePendingOrdersMap.find(id)->second;
				int newId = newId_deltaQty.first;	// newId
				int deltaQuantity = newId_deltaQty.second;	// deltaQuantity

				orderPtr->replaceOrder(newId, deltaQuantity);

				orderPtr->ChangeOrderState(true);	// ChangeOrderState(isPendingUpdate = true));

				updateCOV(orderPtr->Side(), orderPtr->Price() * orderPtr->remainingQuantity);
			}
			else
			{
				// acknowledgement received for order in non-Pending State, log error
			}
		}
		else
		{
			// Order not present, log error
		}
	}

	// Assumption -
	// 1. In case of Replace request rejected, the Id is oldId
	virtual void OnRequestRejected(int id) override
	{
		auto it = orders.find(id);
		if (it != orders.end())
		{
			shared_ptr<Order> orderPtr = it->second;

			if (orderPtr->orderState == OrderState::NewPending)
			{
				orderPtr->remainingQuantity = 0;
				orderPtr->orderState = OrderState::Rejected;
			}
			else if (orderPtr->orderState == OrderState::ReplacePending)
			{
				replacePendingOrdersMap.erase(id);
				orderPtr->ChangeOrderState(true);	// ChangeOrderState(isPendingUpdate = true)

				updateCOV(orderPtr->Side(), orderPtr->Price() * orderPtr->remainingQuantity);
			}
			else
			{
				// Rejection received for non pending order, log error
			}
		}
		else
		{
			// Order not present, log error
		}
	}

	// Assumtions - 
	// 1. Processing fills even when order is in pending state (NewPending or ReplacePending).
	// 2. Allowing Fills more than total quantity (to support additional increased delta quantity of pending replace request) 
	// 3. All fills are recieved as per oldId until pending Replace request is acknowwledged 
	virtual void OnOrderFilled(int id, int quantityFilled) override
	{
		auto it = orders.find(id);
		if (it != orders.end())
		{
			shared_ptr<Order> orderPtr = it->second;

			if (orderPtr->orderState != OrderState::Rejected)
			{
				orderPtr->filledQuantity += quantityFilled;
				orderPtr->remainingQuantity -= quantityFilled;

				updateNFQ(orderPtr->Side(), quantityFilled);
				updateCOV(orderPtr->Side(), -(orderPtr->Price() * quantityFilled));

				orderPtr->ChangeOrderState();
			}
			else
			{
				// fills received for rejected order, log error
			}
		}
	}

	int getNFQ() { return nfq; }
	long double getCOV(char side) { return cov[side == 'B']; }
	long double getPOV(char side) { return pov[side == 'B']; }
};


int main()
{
	OrderManager manager;

	manager.OnInsertOrderRequest(100, 'B', 200, 10);
	manager.OnRequestAcknowledged(100);
	manager.OnOrderFilled(100, 5);
	manager.OnReplaceOrderRequest(100, 101, 2);
	manager.OnOrderFilled(100, 5);
	manager.OnOrderFilled(100, 1);
	manager.OnRequestAcknowledged(100);

	return 0;
}


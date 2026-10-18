#pragma once

#include "CoreMinimal.h"

template <typename ItemType>
class TTDDeferredFeedbackQueue
{
public:
	void Push(const ItemType& Item, const double Now)
	{
		FQueuedItem& QueuedItem = QueuedItems.AddDefaulted_GetRef();
		QueuedItem.Item = Item;
		QueuedItem.QueuedTime = Now;
	}

	template <typename TryProcessType>
	int32 Drain(const double Now, const double MaxQueuedSeconds, TryProcessType&& TryProcess)
	{
		TArray<FQueuedItem> PendingItems = MoveTemp(QueuedItems);
		QueuedItems.Reset();

		TArray<FQueuedItem> RemainingItems;
		int32 ProcessedCount = 0;
		bool bIsBudgetExhausted = false;
		for (FQueuedItem& PendingItem : PendingItems)
		{
			if (Now - PendingItem.QueuedTime > MaxQueuedSeconds)
			{
				continue;
			}
			if (!bIsBudgetExhausted && TryProcess(PendingItem.Item))
			{
				++ProcessedCount;
				continue;
			}
			bIsBudgetExhausted = true;
			RemainingItems.Add(MoveTemp(PendingItem));
		}

		RemainingItems.Append(MoveTemp(QueuedItems));
		QueuedItems = MoveTemp(RemainingItems);
		return ProcessedCount;
	}

	void Reset() { QueuedItems.Reset(); }
	int32 Num() const { return QueuedItems.Num(); }

private:
	struct FQueuedItem
	{
		ItemType Item;
		double QueuedTime = 0.;
	};

	TArray<FQueuedItem> QueuedItems;
};

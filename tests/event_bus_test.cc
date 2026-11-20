// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scripting/event_bus.h"

#include <gtest/gtest.h>

#include <string>

using namespace Mercury;

namespace {

struct Ping {
  int value = 0;
};

struct Pong {
  std::string text;
};

}  // namespace

TEST(EventBus, DeliversToSubscribers) {
  EventBus bus;
  int total = 0;
  auto first =
      bus.Subscribe<Ping>([&](const Ping& event) { total += event.value; });
  auto second = bus.Subscribe<Ping>(
      [&](const Ping& event) { total += event.value * 10; });

  EXPECT_EQ(bus.SubscriberCount<Ping>(), 2u);
  bus.Publish(Ping{3});
  EXPECT_EQ(total, 33);
}

TEST(EventBus, DeliversOnlyTheSubscriptionType) {
  EventBus bus;
  int pings = 0;
  int pongs = 0;
  auto ping = bus.Subscribe<Ping>([&](const Ping&) { pings++; });
  auto pong = bus.Subscribe<Pong>([&](const Pong&) { pongs++; });

  bus.Publish(Ping{1});
  EXPECT_EQ(pings, 1);
  EXPECT_EQ(pongs, 0);

  bus.Publish(Pong{"hello"});
  EXPECT_EQ(pings, 1);
  EXPECT_EQ(pongs, 1);
}

TEST(EventBus, SubscriptionHandleUnsubscribes) {
  EventBus bus;
  int calls = 0;
  {
    auto subscription = bus.Subscribe<Ping>([&](const Ping&) { calls++; });
    bus.Publish(Ping{});
    subscription.Unsubscribe();
    subscription.Unsubscribe();  // idempotent
    bus.Publish(Ping{});
  }
  bus.Publish(Ping{});
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(bus.SubscriberCount(), 0u);
}

TEST(EventBus, MoveTransfersTheSubscription) {
  EventBus bus;
  int calls = 0;
  EventBus::Subscription outer;
  {
    auto inner = bus.Subscribe<Ping>([&](const Ping&) { calls++; });
    outer = std::move(inner);
  }
  // `inner` is gone, but the listener is still owned by `outer`.
  bus.Publish(Ping{});
  EXPECT_EQ(calls, 1);
  outer.Unsubscribe();
  bus.Publish(Ping{});
  EXPECT_EQ(calls, 1);
}

TEST(EventBus, RemoveDuringPublishSkipsTheRemovedListener) {
  EventBus bus;
  int calls = 0;
  EventBus::Subscription second;
  auto first = bus.Subscribe<Ping>([&](const Ping&) {
    calls++;
    second.Unsubscribe();
  });
  second = bus.Subscribe<Ping>([&](const Ping&) { calls += 100; });

  bus.Publish(Ping{});
  EXPECT_EQ(calls, 1);
}

TEST(EventBus, ClearDropsEveryListener) {
  EventBus bus;
  int calls = 0;
  auto ping = bus.Subscribe<Ping>([&](const Ping&) { calls++; });
  auto pong = bus.Subscribe<Pong>([&](const Pong&) { calls++; });
  EXPECT_EQ(bus.SubscriberCount(), 2u);

  bus.Clear();
  bus.Publish(Ping{});
  bus.Publish(Pong{"x"});
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(bus.SubscriberCount(), 0u);
}

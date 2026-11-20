// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * event_bus.h
 *
 * A typed publish/subscribe bus for C++ gameplay code. Behaviours have Event
 * fields for calls saved in a map; EventBus is the code-to-code equivalent the
 * simple depth does not need and the deep depth does (a network system
 * publishing "peer connected", a HUD listening for "score changed").
 *
 *   struct ScoreChanged { int score; };
 *
 *   auto subscription = scene.GetEventBus().Subscribe<ScoreChanged>(
 *       [](const ScoreChanged& event) { UpdateLabel(event.score); });
 *
 *   scene.GetEventBus().Publish(ScoreChanged{42});
 *
 * Subscriptions are RAII: the handle unsubscribes in its destructor, and
 * Unsubscribe() is idempotent. Publishing copies the listener list, so a
 * listener may subscribe or unsubscribe while an event is being delivered;
 * listeners removed during delivery are not called for the remaining
 * recipients of that publish.
 *
 * The bus is owned by the scene (GameScene), so it is shared by every
 * Behaviour and System of that scene. It is not thread-safe: publish on the
 * thread that drives the scene (the main thread).
 */
#ifndef MERCURY_SCRIPTING_EVENT_BUS_H
#define MERCURY_SCRIPTING_EVENT_BUS_H

#include <cstdint>
#include <functional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Mercury {

class EventBus {
 public:
  using SubscriptionId = uint64_t;

  /**
   * @brief Owning handle to one subscription (RAII)
   *
   * Move-only. Destroying it, or calling Unsubscribe(), removes the listener.
   * A moved-from or unsubscribed handle is invalid and does nothing.
   */
  class Subscription {
   public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept
        : bus_(other.bus_), id_(other.id_) {
      other.bus_ = nullptr;
      other.id_ = 0;
    }
    Subscription& operator=(Subscription&& other) noexcept {
      if (this != &other) {
        Reset();
        bus_ = other.bus_;
        id_ = other.id_;
        other.bus_ = nullptr;
        other.id_ = 0;
      }
      return *this;
    }
    ~Subscription() { Reset(); }

    bool IsValid() const { return bus_ != nullptr && id_ != 0; }
    explicit operator bool() const { return IsValid(); }
    SubscriptionId GetId() const { return id_; }

    // Removes the listener now; safe to call more than once.
    void Unsubscribe() { Reset(); }

   private:
    friend class EventBus;
    Subscription(EventBus* bus, SubscriptionId id) : bus_(bus), id_(id) {}

    void Reset() {
      if (bus_ && id_ != 0) bus_->Unsubscribe(id_);
      bus_ = nullptr;
      id_ = 0;
    }

    EventBus* bus_ = nullptr;
    SubscriptionId id_ = 0;
  };

  /**
   * @brief Call `listener` for every event of type E
   * @return a handle that removes the listener when it goes away
   */
  template <typename E, typename F>
    requires std::is_invocable_v<F&, const E&>
  [[nodiscard]] Subscription Subscribe(F&& listener) {
    const SubscriptionId id = next_id_++;
    subscribers_[typeid(E)].push_back(
        {id, [callback = std::forward<F>(listener)](const void* event) mutable {
           callback(*static_cast<const E*>(event));
         }});
    return Subscription(this, id);
  }

  // Calls every current listener of E. Order is subscription order.
  template <typename E>
  void Publish(const E& event) const {
    const std::type_index key(typeid(E));
    auto it = subscribers_.find(key);
    if (it == subscribers_.end()) return;
    // The listener list is copied so callbacks may change the bus while it is
    // being delivered; `Contains` makes removal take effect immediately.
    const std::vector<Entry> listeners = it->second;
    for (const Entry& entry : listeners) {
      if (Contains(key, entry.id)) entry.callback(&event);
    }
  }

  // Removes a subscription by id. Invalid ids are ignored.
  void Unsubscribe(SubscriptionId id) {
    for (auto& [type, entries] : subscribers_) {
      std::erase_if(entries,
                    [id](const Entry& entry) { return entry.id == id; });
    }
  }

  // Number of listeners for one event type.
  template <typename E>
  size_t SubscriberCount() const {
    auto it = subscribers_.find(std::type_index(typeid(E)));
    return it == subscribers_.end() ? 0 : it->second.size();
  }

  // Number of listeners across every event type.
  size_t SubscriberCount() const {
    size_t count = 0;
    for (const auto& [type, entries] : subscribers_) count += entries.size();
    return count;
  }

  // Removes every listener. Handles from before the call stay invalid but
  // harmless.
  void Clear() { subscribers_.clear(); }

 private:
  struct Entry {
    SubscriptionId id = 0;
    std::function<void(const void*)> callback;
  };

  bool Contains(const std::type_index& key, SubscriptionId id) const {
    auto it = subscribers_.find(key);
    if (it == subscribers_.end()) return false;
    for (const Entry& entry : it->second) {
      if (entry.id == id) return true;
    }
    return false;
  }

  mutable std::unordered_map<std::type_index, std::vector<Entry>> subscribers_;
  SubscriptionId next_id_ = 1;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_EVENT_BUS_H

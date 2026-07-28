// SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <linux/can.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

namespace dbc_gen_cpp
{

/// @brief Router for can frames to typed callbacks
/// Supports both standard CAN (exact ID match) and J1939 (PGN match)
class CANHandler
{
public:
  /// @brief Register a callback function for a given can message type
  /// @tparam T
  /// @param cb
  template <typename T>
  void set_handler(std::function<void(const T &)> cb)
  {
    static_assert(T::Id != 0, "CAN ID must be non-zero");
    if constexpr (T::IsJ1939) {
      // Match by PGN for J1939 messages
      j1939_handlers_[T::Pgn] = std::make_unique<CanMessageHandlerImpl<T>>(std::move(cb));
    } else if constexpr (T::IsExtendedFrame) {
      // Include EFF flag when comparing with raw linux/can.h can frames.
      handlers_[T::Id | CAN_EFF_FLAG] = std::make_unique<CanMessageHandlerImpl<T>>(std::move(cb));
    } else {
      // Standard CAN message
      handlers_[T::Id] = std::make_unique<CanMessageHandlerImpl<T>>(std::move(cb));
    }
  }

  /// @brief Route a can frame to the appropriate handler, if registered
  /// @param frame
  /// @return true if there was a registered handler that was called
  bool handle(const can_frame & frame) const
  {
    // Standard CAN: exact ID match
    auto it = handlers_.find(frame.can_id);
    if (it != handlers_.end()) {
      it->second->invoke(frame);
      return true;
    }

    // J1939: extract PGN and match (only for extended frames)
    if (frame.can_id & CAN_EFF_FLAG) {
      uint32_t pgn = extractPgn(frame.can_id);
      auto j1939_it = j1939_handlers_.find(pgn);
      if (j1939_it != j1939_handlers_.end()) {
        j1939_it->second->invoke(frame);
        return true;
      }
    }

    return false;
  }

private:
  /// @brief Extract PGN from a 29-bit J1939 CAN ID
  /// @param can_id The CAN ID with EFF mask already applied
  /// @return The PGN value
  static uint32_t extractPgn(uint32_t can_id)
  {
    uint8_t pf = (can_id >> 16) & 0xFF;
    if (pf >= 240) {
      // PDU2 (broadcast): PGN includes PS field
      return (can_id >> 8) & 0x3FFFF;
    } else {
      // PDU1 (destination-specific): PGN excludes PS field
      return (can_id >> 8) & 0x3FF00;
    }
  }

  /// @brief Type-erased pure virtual interface class for can message callback functions
  struct ICanMessageHandler
  {
    virtual ~ICanMessageHandler() = default;
    virtual void invoke(const can_frame &) const = 0;
  };

  /// @brief Concrete instantiation of the handler interface for a given message type
  /// @tparam T CAN message class
  template <typename T>
  struct CanMessageHandlerImpl : ICanMessageHandler
  {
    std::function<void(const T &)> callback;

    explicit CanMessageHandlerImpl(std::function<void(const T &)> cb)
    : callback(std::move(cb))
    {}

    void invoke(const can_frame & frame) const override
    {
      callback(T(frame));
    }
  };

  std::unordered_map<uint32_t, std::unique_ptr<ICanMessageHandler>> handlers_;  // Standard CAN: keyed by ID
  std::unordered_map<uint32_t, std::unique_ptr<ICanMessageHandler>> j1939_handlers_;  // J1939: keyed by PGN
};

}  // namespace dbc_gen_cpp

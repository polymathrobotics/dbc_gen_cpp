// SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "dbc_gen_cpp/can_handler.hpp"
#include "fake_j1939_can/fake_j1939_can.hpp"
#include "fake_vehicle_can/fake_vehicle_can.hpp"
#include "polymath_test/catch2.hpp"

// ============================================================================
// CAN Encode/Decode Tests (with byte layout verification)
// ============================================================================
// These tests verify exact byte layouts to ensure encoding/decoding is correct.

TEST_CASE("CAN encode - Standard frame byte layout")
{
  SECTION("Single 8-bit signal")
  {
    // SteeringFeedback: angle is 8-bit signed at bits 0-7
    fake_vehicle_can::SteeringFeedback msg{25};
    can_frame frame = msg;

    // Verify the raw byte value
    REQUIRE(frame.can_id == 101);
    REQUIRE(frame.len == 1);
    REQUIRE(frame.data[0] == 25);
  }

  SECTION("Single boolean signal")
  {
    fake_vehicle_can::Heartbeat msg{};
    msg.alive = 1;
    can_frame frame = msg;

    REQUIRE(frame.can_id == 1);
    REQUIRE(frame.len == 1);
    REQUIRE((frame.data[0] & 0x01) == 1);
  }

  SECTION("Multi-signal message byte positions")
  {
    // ExtendedDiagnostic: diagnostic_code (16-bit at 0-15), fault_count (8-bit at 16-23)
    fake_vehicle_can::ExtendedDiagnostic msg{};
    msg.diagnostic_code = 0x1234;
    msg.fault_count = 0x56;
    can_frame frame = msg;

    // Verify EFF flag is set for extended frame
    REQUIRE((frame.can_id & CAN_EFF_FLAG) != 0);
    REQUIRE((frame.can_id & CAN_EFF_MASK) == fake_vehicle_can::ExtendedDiagnostic::Id);

    // Verify little-endian encoding of 16-bit value
    REQUIRE(frame.data[0] == 0x34);  // Low byte of diagnostic_code
    REQUIRE(frame.data[1] == 0x12);  // High byte of diagnostic_code
    REQUIRE(frame.data[2] == 0x56);  // fault_count
  }

  SECTION("Scaled signal encoding")
  {
    // Fuel_Tank_Level: scale=0.0015258789, 16-bit unsigned
    // Value 50.0 / 0.0015258789 ≈ 32768
    fake_vehicle_can::Fuel_Tank_Level msg{50.0};
    can_frame frame = msg;

    uint16_t raw_value = frame.data[0] | (frame.data[1] << 8);
    // 50.0 / 0.0015258789 = 32768.0 (approximately)
    REQUIRE(raw_value == Approx(32768).margin(1));
  }
}

TEST_CASE("CAN decode - Standard frame byte layout")
{
  SECTION("Single unsigned integer signal")
  {
    can_frame frame{};
    frame.can_id = 101;
    frame.can_dlc = 1;
    frame.data[0] = 42;

    fake_vehicle_can::SteeringFeedback msg{frame};
    REQUIRE(msg.angle == 42);
  }

  SECTION("Multi-signal message from raw bytes")
  {
    can_frame frame{};
    frame.can_id = fake_vehicle_can::ExtendedDiagnostic::Id | CAN_EFF_FLAG;
    frame.can_dlc = 8;
    frame.data[0] = 0xCD;  // Low byte of diagnostic_code
    frame.data[1] = 0xAB;  // High byte of diagnostic_code = 0xABCD
    frame.data[2] = 0x07;  // fault_count = 7
    memset(&frame.data[3], 0, 5);

    fake_vehicle_can::ExtendedDiagnostic msg{frame};
    REQUIRE(msg.diagnostic_code == 0xABCD);
    REQUIRE(msg.fault_count == 7);
  }

  SECTION("Scaled signal decoding")
  {
    can_frame frame{};
    frame.can_id = 201;
    frame.can_dlc = 2;
    // Raw value 32768 * 0.0015258789 ≈ 50.0
    frame.data[0] = 0x00;  // Low byte
    frame.data[1] = 0x80;  // High byte = 0x8000 = 32768

    fake_vehicle_can::Fuel_Tank_Level msg{frame};
    REQUIRE(msg.fuel_tank_level == Approx(50.0).margin(0.01));
  }
}

TEST_CASE("CAN encode - J1939 frame byte layout")
{
  SECTION("PDU2 message with multiple signals")
  {
    // EngineData: engine_rpm (16-bit at 0-15), engine_load (8-bit at 16-23)
    fake_j1939_can::EngineData msg{};
    msg.engine_rpm = 1000;
    msg.engine_load = 75;
    can_frame frame = msg;

    // Verify EFF flag is set
    REQUIRE((frame.can_id & CAN_EFF_FLAG) != 0);
    REQUIRE((frame.can_id & CAN_EFF_MASK) == fake_j1939_can::EngineData::Id);

    // Verify raw bytes (little-endian)
    REQUIRE(frame.data[0] == (1000 & 0xFF));  // Low byte of engine_rpm
    REQUIRE(frame.data[1] == ((1000 >> 8) & 0xFF));  // High byte of engine_rpm
    REQUIRE(frame.data[2] == 75);  // engine_load
  }

  SECTION("PDU1 message with scaled signal")
  {
    // BrakeCommand: brake_pressure (16-bit, scale=0.1), brake_mode (8-bit)
    fake_j1939_can::BrakeCommand msg{};
    msg.brake_pressure = 100.0;  // Raw value = 100.0 / 0.1 = 1000
    msg.brake_mode = 5;
    can_frame frame = msg;

    uint16_t raw_pressure = frame.data[0] | (frame.data[1] << 8);
    REQUIRE(raw_pressure == 1000);
    REQUIRE(frame.data[2] == 5);
  }
}

TEST_CASE("CAN decode - J1939 frame byte layout")
{
  SECTION("PDU2 message from raw bytes")
  {
    can_frame frame{};
    frame.can_id = fake_j1939_can::EngineData::Id | CAN_EFF_FLAG;
    frame.can_dlc = 8;
    // engine_rpm = 2500 (0x09C4)
    frame.data[0] = 0xC4;
    frame.data[1] = 0x09;
    // engine_load = 80
    frame.data[2] = 80;
    memset(&frame.data[3], 0, 5);

    fake_j1939_can::EngineData msg{frame};
    REQUIRE(msg.engine_rpm == 2500);
    REQUIRE(msg.engine_load == 80);
  }

  SECTION("PDU2 message from different source address")
  {
    can_frame frame{};
    // Same PGN but different source address (SA=0x42)
    frame.can_id = ((fake_j1939_can::EngineData::Id & ~0xFF) | 0x42) | CAN_EFF_FLAG;
    frame.can_dlc = 8;
    frame.data[0] = 0xE8;  // engine_rpm = 1000 (0x03E8)
    frame.data[1] = 0x03;
    frame.data[2] = 50;  // engine_load = 50
    memset(&frame.data[3], 0, 5);

    fake_j1939_can::EngineData msg{frame};
    REQUIRE(msg.engine_rpm == 1000);
    REQUIRE(msg.engine_load == 50);
  }

  SECTION("PDU1 message with scaled signal from raw bytes")
  {
    can_frame frame{};
    frame.can_id = fake_j1939_can::BrakeCommand::Id | CAN_EFF_FLAG;
    frame.can_dlc = 8;
    // brake_pressure raw = 500, scaled = 500 * 0.1 = 50.0
    frame.data[0] = 0xF4;  // 500 & 0xFF
    frame.data[1] = 0x01;  // 500 >> 8
    frame.data[2] = 3;  // brake_mode = 3
    memset(&frame.data[3], 0, 5);

    fake_j1939_can::BrakeCommand msg{frame};
    REQUIRE(msg.brake_pressure == 50.0);
    REQUIRE(msg.brake_mode == 3);
  }
}

TEST_CASE("CAN roundtrip - Encode/decode with byte verification")
{
  // This test verifies that encoding then decoding produces the same values,
  // but also checks the intermediate raw bytes match expectations.

  SECTION("Standard CAN roundtrip with byte verification")
  {
    fake_vehicle_can::ExtendedDiagnostic original{};
    original.diagnostic_code = 0xBEEF;
    original.fault_count = 0x42;

    can_frame frame = original;

    // Verify raw bytes
    REQUIRE(frame.data[0] == 0xEF);
    REQUIRE(frame.data[1] == 0xBE);
    REQUIRE(frame.data[2] == 0x42);

    // Verify roundtrip
    fake_vehicle_can::ExtendedDiagnostic decoded{frame};
    REQUIRE(decoded.diagnostic_code == 0xBEEF);
    REQUIRE(decoded.fault_count == 0x42);
  }

  SECTION("J1939 roundtrip with byte verification")
  {
    fake_j1939_can::EngineData original{};
    original.engine_rpm = 3500;
    original.engine_load = 90;

    can_frame frame = original;

    // Verify raw bytes (3500 = 0x0DAC)
    REQUIRE(frame.data[0] == 0xAC);
    REQUIRE(frame.data[1] == 0x0D);
    REQUIRE(frame.data[2] == 90);

    // Verify roundtrip
    fake_j1939_can::EngineData decoded{frame};
    REQUIRE(decoded.engine_rpm == 3500);
    REQUIRE(decoded.engine_load == 90);
  }
}

TEST_CASE("CAN encode/decode - Messages with 0 length payload")
{
  SECTION("Standard CAN message with 0 length payload")
  {
    fake_vehicle_can::EmptyMessage msg{};
    can_frame frame = msg;

    REQUIRE(frame.can_id == fake_vehicle_can::EmptyMessage::Id);
    REQUIRE(frame.len == 0);
    REQUIRE(fake_vehicle_can::EmptyMessage::DataLength == 0);
    REQUIRE(fake_vehicle_can::EmptyMessage::IsJ1939 == false);
    REQUIRE(fake_vehicle_can::EmptyMessage::IsExtendedFrame == false);

    // Verify decode doesn't throw
    fake_vehicle_can::EmptyMessage decoded{frame};
    (void)decoded;  // No signals to verify
  }

  SECTION("J1939 message with 0 length payload")
  {
    fake_j1939_can::EmptyJ1939Message msg{};
    can_frame frame = msg;

    REQUIRE((frame.can_id & CAN_EFF_FLAG) != 0);
    REQUIRE((frame.can_id & CAN_EFF_MASK) == fake_j1939_can::EmptyJ1939Message::Id);
    REQUIRE(frame.len == 0);
    REQUIRE(fake_j1939_can::EmptyJ1939Message::DataLength == 0);
    REQUIRE(fake_j1939_can::EmptyJ1939Message::IsJ1939 == true);
    REQUIRE(fake_j1939_can::EmptyJ1939Message::IsExtendedFrame == true);

    // Verify decode doesn't throw
    fake_j1939_can::EmptyJ1939Message decoded{frame};
    (void)decoded;  // No signals to verify
  }
}

// ============================================================================
// Signal Precision Tests
// ============================================================================
// Because some signals with scaling factors are encoded into signals with
// fewer bits than a standard float, some truncation may occur. These tests
// verify that the decoded values are within the expected precision limits
// (aka the scaling factor).

TEST_CASE("CAN encode/decode - Scaled signal precision")
{
  {
    fake_vehicle_can::Fuel_Tank_Level msg(67.55f);
    can_frame frame = msg;
    fake_vehicle_can::Fuel_Tank_Level re_parsed{frame};

    // Margin accounts for precision loss: the DBC scaling factor (0.0015258789) truncates
    // the value to 2 bytes during encoding, then scales it back during decoding.
    REQUIRE(67.55f == Approx(re_parsed.fuel_tank_level).margin(0.0015258789f));
  }

  {
    fake_vehicle_can::Fuel_Tank_Level msg(37.38f);
    can_frame frame = msg;
    fake_vehicle_can::Fuel_Tank_Level re_parsed{frame};

    // Require the signal to be within resolution of 0.0015258789 of the original value
    // since that is the scaling factor defined in the DBC
    REQUIRE(37.38f == Approx(re_parsed.fuel_tank_level).margin(0.0015258789f));
  }
}

// ============================================================================
// EFF and Mismatched ID/PGN Error Handling Tests
// ============================================================================

TEST_CASE("Extended frame ID validation")
{
  SECTION("Extended CAN ID rejects frame without EFF flag")
  {
    can_frame frame;
    // Same ID but WITHOUT EFF flag - should be rejected
    frame.can_id = fake_vehicle_can::ExtendedDiagnostic::Id;  // No CAN_EFF_FLAG
    frame.can_dlc = 8;
    memset(frame.data, 0, 8);

    REQUIRE_THROWS_WITH(
      fake_vehicle_can::ExtendedDiagnostic{frame}, "CAN Frame ID does not match this message type ID");
  }

  SECTION("Extended CAN ID doesn't match arbitrary standard ID")
  {
    // ExtendedDiagnostic::Id = 0x800000C8 (includes extended frame indicator from DBC)
    // This tests that an arbitrary standard frame won't be parsed as an extended frame

    can_frame standard_frame;
    standard_frame.can_id = 0xC8;  // Standard frame with same lower bits as ExtendedDiagnostic
    standard_frame.can_dlc = 8;
    memset(standard_frame.data, 0, 8);

    // Should throw because IDs don't match (one is standard, one is extended)
    REQUIRE_THROWS_WITH(
      fake_vehicle_can::ExtendedDiagnostic{standard_frame}, "CAN Frame ID does not match this message type ID");
  }
}

TEST_CASE("J1939 constructor rejects wrong PGN")
{
  can_frame frame{};
  frame.can_dlc = 8;
  memset(frame.data, 0, 8);

  // Wrong PGN for EngineData (PGN 61445 instead of 61444)
  frame.can_id = 0x0CF00500 | CAN_EFF_FLAG;
  REQUIRE_THROWS(fake_j1939_can::EngineData{frame});

  // Wrong PGN for TorqueCommand (PF=0xCB instead of 0xCA)
  frame.can_id = 0x18CBFF00 | CAN_EFF_FLAG;
  REQUIRE_THROWS(fake_j1939_can::TorqueCommand{frame});
}

TEST_CASE("Standard CAN ID mismatch throws")
{
  can_frame frame;
  frame.can_id = 100;
  frame.can_dlc = 4;
  memset(frame.data, 0, 8);

  REQUIRE_THROWS(fake_vehicle_can::SteeringFeedback{frame});
}

// ============================================================================
// J1939 Protocol Parsing Tests
// ============================================================================

TEST_CASE("J1939 static constant parsing")
{
  // PDU2 broadcast message
  REQUIRE(fake_j1939_can::EngineData::IsJ1939 == true);
  REQUIRE(fake_j1939_can::EngineData::Pgn == 61444);
  REQUIRE(fake_j1939_can::EngineData::DefaultPriority == 3);
  REQUIRE(fake_j1939_can::EngineData::SourceAddress == 0);
  REQUIRE(fake_j1939_can::EngineData::IsPduBroadcast == true);

  // Another PDU2 broadcast message
  REQUIRE(fake_j1939_can::VehicleSpeed::IsJ1939 == true);
  REQUIRE(fake_j1939_can::VehicleSpeed::Pgn == 65265);
  REQUIRE(fake_j1939_can::VehicleSpeed::DefaultPriority == 6);
  REQUIRE(fake_j1939_can::VehicleSpeed::IsPduBroadcast == true);

  // PDU1 destination-specific message
  REQUIRE(fake_j1939_can::TorqueCommand::IsJ1939 == true);
  REQUIRE(fake_j1939_can::TorqueCommand::Pgn == 51712);
  REQUIRE(fake_j1939_can::TorqueCommand::DefaultPriority == 6);
  REQUIRE(fake_j1939_can::TorqueCommand::IsPduBroadcast == false);
  // PDU1 messages have a default destination address from the DBC
  REQUIRE(fake_j1939_can::TorqueCommand::DefaultDestinationAddress == 0xFF);

  // BrakeCommand is a PDU1 message with SourceAddress=0x25 (Changes how we parse ID, so test it specifically)
  REQUIRE(fake_j1939_can::BrakeCommand::IsJ1939 == true);
  REQUIRE(fake_j1939_can::BrakeCommand::Pgn == 51200);  // 0xC800
  REQUIRE(fake_j1939_can::BrakeCommand::DefaultPriority == 3);
  REQUIRE(fake_j1939_can::BrakeCommand::SourceAddress == 0x25);  // Specific SA: 37
  REQUIRE(fake_j1939_can::BrakeCommand::IsPduBroadcast == false);
  REQUIRE(fake_j1939_can::BrakeCommand::DefaultDestinationAddress == 0x10);  // DA: 16

  // Non-J1939 message
  REQUIRE(fake_vehicle_can::Heartbeat::IsJ1939 == false);
}

TEST_CASE("J1939 PGN matching logic for PDU1 and PDU2 messages")
{
  SECTION("PDU2 - Broadcast PGNs")
  {
    // Test different priority and source address variations with same PGN
    // Original ID
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0x0CF00400));
    // Different source address (SA=0x0B)
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0x0CF0040B));
    // Different source address (SA=0xFF)
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0x0CF004FF));
    // Different priority (priority=6 instead of 3)
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0x18F00400));
    // Different priority (priority=0)
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0x00F00400));
    // Different EFF/RTR/ERR bits
    REQUIRE(fake_j1939_can::EngineData::matchesPgn(0xECF00400));

    // Test different PGNs
    // Different PS (PS=0x05 instead of 0x04 -> PGN 61445)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x0CF00500));
    // Different PS (PS=0x00 -> PGN 61440)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x0CF00000));
    // Different PF (PF=0xF1 instead of 0xF0 -> PGN 61700)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x0CF10400));
    // Different Data Page (DP=1 -> PGN 126980)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x0DF00400));
    // Different Reserved/EDP bit (R=1 -> PGN 192516)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x0EF00400));
    // Completely different PGN (VehicleSpeed PGN 65265)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x18FEF100));
    // TorqueCommand PGN (PDU1, PGN 51712)
    REQUIRE_FALSE(fake_j1939_can::EngineData::matchesPgn(0x18CAFF00));
  }

  SECTION("PDU1 destination-specific PGNs")
  {
    // Test different priority, source address, and destination variations with same PGN
    // Original ID
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CAFF00));
    // Different source address (SA=0x05)
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CAFF05));
    // Different destination address (PS=0x0A instead of 0xFF)
    // For PDU1, PS is destination, NOT part of PGN
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CA0A00));
    // Different destination (PS=0x00 = broadcast)
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CA0000));
    // Both different destination and source
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CA1234));
    // Different priority (priority=3 instead of 6)
    REQUIRE(fake_j1939_can::TorqueCommand::matchesPgn(0x0CCAFF00));

    // Test different PGNs
    // Different PF (PF=0xCB instead of 0xCA -> PGN 51968)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x18CBFF00));
    // Different PF (PF=0xC9 -> PGN 51456)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x18C9FF00));
    // Different Data Page (DP=1 -> PGN 117248)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x19CAFF00));
    // Different Reserved/EDP bit (R=1 -> PGN 182784)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x1ACAFF00));
    // Completely different PGN (EngineData PGN 61444)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x0CF00400));
    // VehicleSpeed PGN 65265 (PDU2)
    REQUIRE_FALSE(fake_j1939_can::TorqueCommand::matchesPgn(0x18FEF100));
  }
}

TEST_CASE("Ensure J1939 constructor accepts different source address")
{
  can_frame frame{};
  // EngineData from SA=0x0B (different from DBC's SA=0x00)
  frame.can_id = 0x0CF0040B | CAN_EFF_FLAG;
  frame.can_dlc = 8;
  memset(frame.data, 0, 8);
  // engine_rpm = 1000 (0x03E8) little-endian at bits 0-15
  frame.data[0] = 0xE8;
  frame.data[1] = 0x03;
  // engine_load = 75 at bits 16-23
  frame.data[2] = 75;

  fake_j1939_can::EngineData msg{frame};
  REQUIRE(msg.engine_rpm == 1000);
  REQUIRE(msg.engine_load == 75);
}

// Test that it will accept message routed to different destination addresses
// than what is in the DBC for PDU1 messages. I believe this is the correct behavior
// as just an observer on the bus.
TEST_CASE("J1939 PDU1 constructor accepts different destination")
{
  can_frame frame{};
  // TorqueCommand to destination 0x0A (PS=0x0A), from SA=0x05
  frame.can_id = 0x18CA0A05 | CAN_EFF_FLAG;
  frame.can_dlc = 8;
  // torque_request = 200 at bits 0-7
  frame.data[0] = 200;

  fake_j1939_can::TorqueCommand msg{frame};
  REQUIRE(msg.torque_request == 200);
}

TEST_CASE("Mixed J1939 and Standard DBC Files")
{
  // Check that EngineAuxStatus shows up as J1939 in the non-J1939 DBC
  REQUIRE(fake_vehicle_can::EngineAuxStatus::IsJ1939);

  // Check that BatteryStatus shows up as not J1939 in the J1939 DBC
  REQUIRE_FALSE(fake_j1939_can::BatteryStatus::IsJ1939);
}

// ============================================================================
// CANHandler Routing Tests
// ============================================================================

// Helper to build a J1939 CAN ID with custom source address
static uint32_t make_j1939_id(uint32_t base_id, uint8_t source_address)
{
  return ((base_id & ~0xFF) | source_address) | CAN_EFF_FLAG;
}

// Helper to build a J1939 PDU1 CAN ID with custom source and destination
static uint32_t make_j1939_pdu1_id(uint32_t base_id, uint8_t dest_address, uint8_t source_address)
{
  return ((base_id & ~0xFFFF) | (dest_address << 8) | source_address) | CAN_EFF_FLAG;
}

// --- Standard CAN Routing Tests ---

TEST_CASE("CANHandler Standard CAN - Basic routing")
{
  using fake_vehicle_can::SteeringFeedback;

  dbc_gen_cpp::CANHandler handler;
  int received_value = 0;
  bool callback_invoked = false;

  handler.set_handler<SteeringFeedback>([&](const SteeringFeedback & msg) {
    callback_invoked = true;
    received_value = msg.angle;
  });

  // Create and send a frame
  SteeringFeedback msg{42};
  can_frame frame = msg;

  bool result = handler.handle(frame);

  REQUIRE(result == true);
  REQUIRE(callback_invoked == true);
  REQUIRE(received_value == 42);
}

TEST_CASE("CANHandler Standard CAN - Multiple handlers")
{
  using fake_vehicle_can::Heartbeat;
  using fake_vehicle_can::SpeedFeedback;
  using fake_vehicle_can::SteeringFeedback;

  dbc_gen_cpp::CANHandler handler;

  int heartbeat_count = 0;
  float speed_value = 0;
  int steering_value = 0;

  handler.set_handler<Heartbeat>([&](const Heartbeat &) { heartbeat_count++; });
  handler.set_handler<SpeedFeedback>([&](const SpeedFeedback & msg) { speed_value = msg.speed; });
  handler.set_handler<SteeringFeedback>([&](const SteeringFeedback & msg) { steering_value = msg.angle; });

  // Send each message type
  handler.handle(Heartbeat{});
  handler.handle(Heartbeat{});

  SpeedFeedback speed_msg{};
  speed_msg.speed = 5.5f;
  handler.handle(speed_msg);

  handler.handle(SteeringFeedback{30});

  REQUIRE(heartbeat_count == 2);
  REQUIRE(speed_value == 5.5f);
  REQUIRE(steering_value == 30);
}

TEST_CASE("CANHandler Standard CAN - Unregistered ID returns false")
{
  dbc_gen_cpp::CANHandler handler;

  // Don't register any handlers
  can_frame frame{};
  frame.can_id = 100;  // SpeedFeedback ID, but no handler registered
  frame.can_dlc = 4;
  memset(frame.data, 0, 8);

  bool result = handler.handle(frame);
  REQUIRE(result == false);
}

TEST_CASE("CANHandler Standard CAN - Handler replacement")
{
  using fake_vehicle_can::SteeringFeedback;

  dbc_gen_cpp::CANHandler handler;

  int first_callback_count = 0;
  int second_callback_count = 0;

  // Register first handler
  handler.set_handler<SteeringFeedback>([&](const SteeringFeedback &) { first_callback_count++; });

  handler.handle(SteeringFeedback{10});
  REQUIRE(first_callback_count == 1);
  REQUIRE(second_callback_count == 0);

  // Replace with second handler
  handler.set_handler<SteeringFeedback>([&](const SteeringFeedback &) { second_callback_count++; });

  handler.handle(SteeringFeedback{20});
  REQUIRE(first_callback_count == 1);  // Should not increase
  REQUIRE(second_callback_count == 1);
}

TEST_CASE("CANHandler Standard CAN - Extended frame flag handling")
{
  using fake_vehicle_can::ExtendedDiagnostic;

  dbc_gen_cpp::CANHandler handler;
  bool callback_invoked = false;
  uint16_t received_code = 0;

  handler.set_handler<ExtendedDiagnostic>([&](const ExtendedDiagnostic & msg) {
    callback_invoked = true;
    received_code = msg.diagnostic_code;
  });

  // Create extended frame message
  ExtendedDiagnostic msg{};
  msg.diagnostic_code = 0x1234;
  msg.fault_count = 3;
  can_frame frame = msg;

  // Verify frame has EFF flag
  REQUIRE((frame.can_id & CAN_EFF_FLAG) != 0);

  bool result = handler.handle(frame);
  REQUIRE(result == true);
  REQUIRE(callback_invoked == true);
  REQUIRE(received_code == 0x1234);
}

// --- J1939 Routing Tests ---

TEST_CASE("CANHandler J1939 - Basic PGN routing")
{
  using fake_j1939_can::EngineData;

  dbc_gen_cpp::CANHandler handler;
  bool callback_invoked = false;
  double received_rpm = 0;

  handler.set_handler<EngineData>([&](const EngineData & msg) {
    callback_invoked = true;
    received_rpm = msg.engine_rpm;
  });

  // Create J1939 message
  EngineData msg{};
  msg.engine_rpm = 2500;
  msg.engine_load = 75;
  can_frame frame = msg;

  bool result = handler.handle(frame);
  REQUIRE(result == true);
  REQUIRE(callback_invoked == true);
  REQUIRE(received_rpm == 2500);
}

TEST_CASE("CANHandler J1939 - Different source addresses route to same handler")
{
  using fake_j1939_can::EngineData;

  dbc_gen_cpp::CANHandler handler;
  int callback_count = 0;

  handler.set_handler<EngineData>([&](const EngineData &) { callback_count++; });

  // Create base frame
  EngineData msg{};
  msg.engine_rpm = 1000;
  msg.engine_load = 50;
  can_frame frame = msg;

  // Send from different source addresses
  frame.can_id = make_j1939_id(EngineData::Id, 0x00);
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_id(EngineData::Id, 0x0B);
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_id(EngineData::Id, 0xFF);
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_id(EngineData::Id, 0x2A);
  REQUIRE(handler.handle(frame) == true);

  REQUIRE(callback_count == 4);
}

TEST_CASE("CANHandler J1939 - PDU1 routes with different destination addresses")
{
  using fake_j1939_can::BrakeCommand;

  dbc_gen_cpp::CANHandler handler;
  int callback_count = 0;

  handler.set_handler<BrakeCommand>([&](const BrakeCommand &) { callback_count++; });

  // Create base frame
  BrakeCommand msg{};
  msg.brake_pressure = 50.0;
  msg.brake_mode = 1;
  can_frame frame = msg;

  // Send to different destination addresses (PDU1: PS field is destination)
  // BrakeCommand base ID: 0x0CC81025 (DA=0x10, SA=0x25)
  frame.can_id = make_j1939_pdu1_id(BrakeCommand::Id, 0x10, 0x25);  // Original
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_pdu1_id(BrakeCommand::Id, 0x05, 0x25);  // Different dest
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_pdu1_id(BrakeCommand::Id, 0xFF, 0x25);  // Broadcast dest
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_pdu1_id(BrakeCommand::Id, 0x00, 0x30);  // Different dest and source
  REQUIRE(handler.handle(frame) == true);

  REQUIRE(callback_count == 4);
}

TEST_CASE("CANHandler J1939 - PDU2 broadcast routing")
{
  using fake_j1939_can::VehicleSpeed;

  dbc_gen_cpp::CANHandler handler;
  int callback_count = 0;
  double last_speed = 0;

  handler.set_handler<VehicleSpeed>([&](const VehicleSpeed & msg) {
    callback_count++;
    last_speed = msg.speed;
  });

  // VehicleSpeed is PDU2 (PF >= 240)
  REQUIRE(VehicleSpeed::IsPduBroadcast == true);

  VehicleSpeed msg{100};
  can_frame frame = msg;

  // Test with different source addresses
  frame.can_id = make_j1939_id(VehicleSpeed::Id, 0x00);
  REQUIRE(handler.handle(frame) == true);

  frame.can_id = make_j1939_id(VehicleSpeed::Id, 0xFE);
  REQUIRE(handler.handle(frame) == true);

  REQUIRE(callback_count == 2);
  REQUIRE(last_speed == 100);
}

TEST_CASE("CANHandler J1939 - Multiple PGN handlers")
{
  using fake_j1939_can::BrakeCommand;
  using fake_j1939_can::EngineData;
  using fake_j1939_can::VehicleSpeed;

  dbc_gen_cpp::CANHandler handler;

  double engine_rpm = 0;
  double vehicle_speed = 0;
  double brake_pressure = 0;

  handler.set_handler<EngineData>([&](const EngineData & msg) { engine_rpm = msg.engine_rpm; });
  handler.set_handler<VehicleSpeed>([&](const VehicleSpeed & msg) { vehicle_speed = msg.speed; });
  handler.set_handler<BrakeCommand>([&](const BrakeCommand & msg) { brake_pressure = msg.brake_pressure; });

  // Send each message type
  EngineData engine_msg{};
  engine_msg.engine_rpm = 3000;
  engine_msg.engine_load = 80;
  handler.handle(static_cast<can_frame>(engine_msg));

  handler.handle(static_cast<can_frame>(VehicleSpeed{65}));

  BrakeCommand brake_msg{};
  brake_msg.brake_pressure = 25.5;
  brake_msg.brake_mode = 2;
  handler.handle(static_cast<can_frame>(brake_msg));

  REQUIRE(engine_rpm == 3000);
  REQUIRE(vehicle_speed == 65);
  REQUIRE(brake_pressure == 25.5);
}

TEST_CASE("CANHandler With Mixed Standard and J1939 Messages")
{
  using fake_j1939_can::BatteryStatus;  // Standard CAN in J1939 DBC
  using fake_j1939_can::EngineData;  // J1939

  dbc_gen_cpp::CANHandler handler;

  double battery_voltage = 0;
  double engine_rpm = 0;

  handler.set_handler<BatteryStatus>([&](const BatteryStatus & msg) { battery_voltage = msg.battery_voltage; });
  handler.set_handler<EngineData>([&](const EngineData & msg) { engine_rpm = msg.engine_rpm; });

  // Verify BatteryStatus is standard CAN
  REQUIRE(BatteryStatus::IsJ1939 == false);
  // Verify EngineData is J1939
  REQUIRE(EngineData::IsJ1939 == true);

  // Send standard CAN message
  BatteryStatus battery_msg{};
  battery_msg.battery_voltage = 12.5;
  battery_msg.battery_soc = 80;
  REQUIRE(handler.handle(static_cast<can_frame>(battery_msg)) == true);

  // Send J1939 message
  EngineData engine_msg{};
  engine_msg.engine_rpm = 1500;
  engine_msg.engine_load = 60;
  REQUIRE(handler.handle(static_cast<can_frame>(engine_msg)) == true);

  REQUIRE(battery_voltage == 12.5);
  REQUIRE(engine_rpm == 1500);
}

TEST_CASE("CANHandler, Ensure Standard CAN lookup happens before J1939")
{
  // This tests that if a standard CAN handler is registered, it takes priority
  // over J1939 PGN matching for frames that happen to have the same raw ID

  using fake_vehicle_can::ExtendedDiagnostic;

  dbc_gen_cpp::CANHandler handler;
  bool extended_handler_called = false;

  handler.set_handler<ExtendedDiagnostic>([&](const ExtendedDiagnostic &) { extended_handler_called = true; });

  // ExtendedDiagnostic uses extended frames but is NOT J1939
  REQUIRE(ExtendedDiagnostic::IsJ1939 == false);

  ExtendedDiagnostic msg{};
  msg.diagnostic_code = 100;
  msg.fault_count = 1;
  can_frame frame = msg;

  // The frame has EFF flag, but should be handled by standard CAN lookup
  // because ExtendedDiagnostic is registered in the standard handlers map
  bool result = handler.handle(frame);

  REQUIRE(result == true);
  REQUIRE(extended_handler_called == true);
}

TEST_CASE("CANHandler Ensure Non-extended frame skips J1939 lookup")
{
  using fake_j1939_can::EngineData;

  dbc_gen_cpp::CANHandler handler;
  bool j1939_handler_called = false;

  handler.set_handler<EngineData>([&](const EngineData &) { j1939_handler_called = true; });

  // Create a standard frame (no EFF flag) with an ID that could be
  // misinterpreted if J1939 lookup wasn't gated by EFF flag check
  can_frame frame{};
  frame.can_id = 0x100;  // Standard CAN ID, no EFF flag
  frame.can_dlc = 8;
  memset(frame.data, 0, 8);

  bool result = handler.handle(frame);

  // Should return false (no standard handler for 0x100)
  // and J1939 handler should NOT be called because EFF flag is not set
  REQUIRE(result == false);
  REQUIRE(j1939_handler_called == false);
}

TEST_CASE("CANHandler Ensure Empty handler returns false")
{
  dbc_gen_cpp::CANHandler handler;
  // No handlers registered

  // Test with standard CAN frame
  can_frame std_frame{};
  std_frame.can_id = 100;
  std_frame.can_dlc = 4;
  memset(std_frame.data, 0, 8);
  REQUIRE(handler.handle(std_frame) == false);

  // Test with J1939 frame
  can_frame j1939_frame{};
  j1939_frame.can_id = 0x0CF00400 | CAN_EFF_FLAG;  // EngineData PGN
  j1939_frame.can_dlc = 8;
  memset(j1939_frame.data, 0, 8);
  REQUIRE(handler.handle(j1939_frame) == false);
}

TEST_CASE("CANHandler routes messages with 0 length payload")
{
  SECTION("Standard CAN message with 0 length payload")
  {
    using fake_vehicle_can::EmptyMessage;

    dbc_gen_cpp::CANHandler handler;
    bool callback_invoked = false;

    handler.set_handler<EmptyMessage>([&](const EmptyMessage &) { callback_invoked = true; });

    EmptyMessage msg{};
    can_frame frame = msg;

    REQUIRE(handler.handle(frame) == true);
    REQUIRE(callback_invoked == true);
  }

  SECTION("J1939 message with 0 length payload")
  {
    using fake_j1939_can::EmptyJ1939Message;

    dbc_gen_cpp::CANHandler handler;
    bool callback_invoked = false;

    handler.set_handler<EmptyJ1939Message>([&](const EmptyJ1939Message &) { callback_invoked = true; });

    EmptyJ1939Message msg{};
    can_frame frame = msg;

    // Verify it's treated as J1939
    REQUIRE(EmptyJ1939Message::IsJ1939 == true);
    REQUIRE((frame.can_id & CAN_EFF_FLAG) != 0);

    REQUIRE(handler.handle(frame) == true);
    REQUIRE(callback_invoked == true);
  }
}

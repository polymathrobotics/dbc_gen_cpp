# DBC CAN Message Type Generator for C++

Extends the code generation capability of `cantools`, which outputs C sources from DBC frame definitions, to create idiomatic C++ interfaces for using CAN message types.

## Usage

In `package.xml`, you'll need this as a `build_depend`.

In your `CMakeLists.txt`:

```cmake
find_package(dbc_gen_cpp REQUIRED)
...

generate_dbc_cpp(my_can_library_name
  DBC ${CMAKE_CURRENT_SOURCE_DIR}/database.dbc
)

...

target_link_libraries(my_library PUBLIC my_can_library_name)
```

### CAN Messages

Message structs are defined in the library name's namespace.

They can be implicitly converted to `can_frame` from `<linux/can.h>`

```c++
#include "my_can_library_name/my_can_library_name.hpp"

...

my_can_library_name::MessageName message{value};
my_can_socket->send(message);
can_frame frame = message;
my_can_socket->send(frame);

```

### Signal Value Maps (Enums)

DBC value tables (`VAL_` lines, and the global `VAL_TABLE_`) map raw signal values
to human-readable states. For every signal that has one, an `enum class` is generated
alongside the message structs, in the library's namespace.

Enums are named `<MessageName>_<SignalName>` (top-level and prefixed, so the same
signal name in different messages never collides). A matching `to_string()` overload
returns the original DBC label text.

Signal fields on the message struct stay as raw physical values (`double`) — the enum
is **additive**, so you opt in by casting when you want the named value:

```c++
#include "my_can_library_name/my_can_library_name.hpp"

// Given a DBC message TransmissionStatus with a signal `gear` whose VAL_ table is
//   VAL_ <id> gear 0 "Neutral" 1 "Drive" 2 "Reverse" ... ;
my_can_library_name::TransmissionStatus msg{frame};

auto gear = static_cast<my_can_library_name::TransmissionStatus_gear>(
              static_cast<int>(msg.gear));

if (gear == my_can_library_name::TransmissionStatus_gear::REVERSE) {
  // ...
}

// to_string() returns the original label from the DBC, handy for logging.
printf("gear = %s\n", my_can_library_name::to_string(gear));  // e.g. "Reverse"
```

Enumerator names come from the DBC label text, uppercased with non-alphanumeric
characters turned into underscores (matching cantools' C `..._CHOICE` macros). A few
labels are adjusted so they remain valid, unique C++ identifiers:

- duplicate labels are de-duplicated by appending their raw value
  (`"Reserved"` at 3 and 4 → `RESERVED_3`, `RESERVED_4`);
- labels starting with a digit get a leading underscore (`"4wd mode"` → `_4WD_MODE`);
- doubled and trailing underscores are collapsed/stripped
  (`"Truck system with fault, stop!"` → `TRUCK_SYSTEM_WITH_FAULT_STOP`).

`to_string()` always returns the unmodified label, regardless of these adjustments.

### CAN Handler - Receive/Subscribe to CAN Messages

A helper class `dbc_gen_cpp::CANHandler` is provided.

Simply register a handler function for a type via `set_handler`, then forward all `can_frame`s received to the `handle()` method to trigger the registered handler functions with the typed structs.

```c++
dbc_gen_cpp::CANHandler handler;
handler.set_handler<my_can_library_name::MessageName>(
  [](const my_can_library_name::MessageName & message) {
    printf('Received MessageName (value %f)\n', message.value);
  });

my_can_socket.on_receive(
  [&](can_frame frame) {
    handler.handle(frame);
  });
```

### J1939-Specific Handling

#### **How is J1939 Defined in DBCs**
DBC files indicate whether a message uses **Standard CAN** or **J1939** via the two lines below.
The first line defines the attribute itself. The second line is applied **per message** and
should appear once for each message that is intended to be treated as J1939, using that
message’s specific CAN ID.

```
BA_DEF_ BO_ "VFrameFormat" ENUM "StandardCAN","ExtendedCAN","reserved","J1939PG";
BA_ "VFrameFormat" BO_ 2364539904 3;
```

#### **J1939 Specific Constants**
The following will only be defined if a message is labeled as J1939 in the DBC:
- static constexpr uint32_t Pgn
- static constexpr uint8_t DefaultPriority = 3;
- static constexpr uint8_t SourceAddress = 37;
- static constexpr bool IsPduBroadcast = true;

The following will only be defined if the message is J1939 and it's of type PDU1 (destination specific):
- static constexpr uint8_t DefaultDestinationAddress

> Note: If you want to safely test if a message is of the J1939 Standard, use the variable `IsJ1939`.

#### **J1939 Logic Changes**
1. When passing a can frame into the explicit constructor, it will only check if the PGN of the incoming frame matches, instead of the whole CAN ID.

# Tests for dbc_gen_cpp

Since [`dbc_gen_cpp`](dbc_gen_cpp/) provides mostly functionality via the `install/` space with CMake functions and a Python package with importlib-registered Jinja templates, it's not possible to test the full usage of that package internally.

`test_dbc_gen_cpp` is fully dedicated to providing tests, it is not meant to be used as a dependency by any package.

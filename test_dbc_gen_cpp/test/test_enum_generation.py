# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
"""Unit tests for the enum/value-map generation in dbc_gen_cpp.

These exercise the Python generation layer directly (the compiled Catch2 tests
cover the emitted C++). In particular they lock in the "fail loudly on an
ambiguous name" behaviour, which cannot be tested through a generated header
because a colliding DBC would simply fail to compile.
"""

import textwrap

import pytest
from cantools import database
from cantools.database.can.c_source import CodeGenMessage

from dbc_gen_cpp.generate_cpp import _enum_underlying_type, build_signal_enums


def _build_enums(dbc_text, tmp_path):
    """Load an inline DBC and run it through build_signal_enums."""
    path = tmp_path / 'in.dbc'
    path.write_text(textwrap.dedent(dbc_text))
    dbase = database.load_file(str(path))
    enums = []
    for message in dbase.messages:
        enums.extend(build_signal_enums(message, CodeGenMessage(message)))
    return enums


# Enums are nested per message, so collisions are now scoped to a single struct:
# signals "gear" and "Gear" in one message both PascalCase to the enum type "Gear".
COLLIDING_DBC = """\
    VERSION ""
    NS_ :
    BS_:
    BU_: N
    BO_ 102 GearStatus: 2 N
     SG_ gear : 0|8@1+ (1,0) [0|255] "" N
     SG_ Gear : 8|8@1+ (1,0) [0|255] "" N
    VAL_ 102 gear 0 "x" 1 "y" ;
    VAL_ 102 Gear 0 "p" 1 "q" ;
"""

# gear exercises every enumerator path: normal, duplicate label ("Reserved"),
# leading-digit label ("4wd mode") and a trailing-punctuation label ("Park!").
GEAR_DBC = """\
    VERSION ""
    NS_ :
    BS_:
    BU_: N
    BO_ 102 GearStatus: 1 N
     SG_ gear : 0|8@1+ (1,0) [0|255] "" N
    VAL_ 102 gear 0 "Neutral" 3 "Reserved" 4 "Reserved" 5 "4wd mode" 6 "Park!" ;
"""


@pytest.mark.parametrize(
    'values, expected',
    [
        ([0, 1, 2, 3], 'uint8_t'),
        ([0, 255], 'uint8_t'),
        ([0, 256], 'uint16_t'),
        ([0, 65535], 'uint16_t'),
        ([0, 65536], 'uint32_t'),
        ([1, 2, 2147483648], 'uint32_t'),  # 2^31 flag: stays unsigned 32-bit
        ([0, 4294967296], 'uint64_t'),  # 2^32: needs 64-bit
        ([-1, 0, 1], 'int8_t'),  # any negative -> signed
        ([-128, 127], 'int8_t'),
        ([-129, 0], 'int16_t'),
        ([-1, 2147483647], 'int32_t'),
    ],
)
def test_enum_underlying_type_sizing(values, expected):
    """The underlying type is the smallest stdint type spanning the choice values."""
    assert _enum_underlying_type(sorted(values)) == expected


def test_colliding_enum_names_raise(tmp_path):
    """Two signals whose PascalCase enum names match within a message must fail loudly."""
    with pytest.raises(ValueError, match='collides') as excinfo:
        _build_enums(COLLIDING_DBC, tmp_path)
    # The error names both offending signals so the ambiguity is decipherable.
    message = str(excinfo.value)
    assert 'GearStatus.gear' in message
    assert 'GearStatus.Gear' in message


def test_value_map_generates_enum(tmp_path):
    """A normal value table produces one enum with decipherable enumerators."""
    enums = _build_enums(GEAR_DBC, tmp_path)
    assert len(enums) == 1

    enum = enums[0]
    assert enum['name'] == 'Gear'
    assert enum['underlying_type'] == 'uint8_t'

    by_value = {v['value']: v for v in enum['enumerators']}
    assert by_value[0]['ident'] == 'NEUTRAL'
    # Duplicated "Reserved" label is de-duplicated by raw value (still decipherable).
    assert by_value[3]['ident'] == 'RESERVED_3'
    assert by_value[4]['ident'] == 'RESERVED_4'
    # Leading digit is prefixed to stay a valid identifier.
    assert by_value[5]['ident'] == '_4WD_MODE'
    # Trailing punctuation is stripped, but to_string keeps the original label.
    assert by_value[6]['ident'] == 'PARK'
    assert by_value[6]['label'] == 'Park!'
    assert by_value[3]['label'] == 'Reserved'

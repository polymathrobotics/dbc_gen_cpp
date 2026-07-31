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

from dbc_gen_cpp.generate_cpp import build_signal_enums


def _build_enums(dbc_text, tmp_path):
    """Load an inline DBC and run it through build_signal_enums."""
    path = tmp_path / 'in.dbc'
    path.write_text(textwrap.dedent(dbc_text))
    dbase = database.load_file(str(path))
    used_enum_names = {}
    enums = []
    for message in dbase.messages:
        enums.extend(build_signal_enums(message, CodeGenMessage(message), used_enum_names))
    return enums


# Message "A_B" signal "C" and message "A" signal "B_C" both sanitize to A_B_C.
COLLIDING_DBC = """\
    VERSION ""
    NS_ :
    BS_:
    BU_: N
    BO_ 100 A_B: 1 N
     SG_ C : 0|8@1+ (1,0) [0|255] "" N
    BO_ 101 A: 1 N
     SG_ B_C : 0|8@1+ (1,0) [0|255] "" N
    VAL_ 100 C 0 "x" 1 "y" ;
    VAL_ 101 B_C 0 "p" 1 "q" ;
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


def test_colliding_enum_names_raise(tmp_path):
    """Two signals whose sanitized <Message>_<Signal> names match must fail loudly."""
    with pytest.raises(ValueError, match='collides') as excinfo:
        _build_enums(COLLIDING_DBC, tmp_path)
    # The error names both offending signals so the ambiguity is decipherable.
    message = str(excinfo.value)
    assert 'A_B.C' in message
    assert 'A.B_C' in message


def test_value_map_generates_enum(tmp_path):
    """A normal value table produces one enum with decipherable enumerators."""
    enums = _build_enums(GEAR_DBC, tmp_path)
    assert len(enums) == 1

    enum = enums[0]
    assert enum['name'] == 'GearStatus_gear'
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

# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
import argparse
import importlib.resources
import re
from pathlib import Path

from cantools import database
from cantools.database.can.c_source import CodeGenMessage, camel_to_snake_case
from cantools.database.can.c_source import generate as generate_c_source
from jinja2 import Environment


def parse_j1939_id(frame_id):
    """Parse J1939 fields from a 29-bit CAN ID."""
    priority = (frame_id >> 26) & 0x7
    pf = (frame_id >> 16) & 0xFF
    ps = (frame_id >> 8) & 0xFF
    source_address = frame_id & 0xFF
    r = (frame_id >> 25) & 0x1
    dp = (frame_id >> 24) & 0x1
    is_pdu_broadcast = pf >= 240
    if is_pdu_broadcast:
        pgn = (r << 17) | (dp << 16) | (pf << 8) | ps
    else:
        pgn = (r << 17) | (dp << 16) | (pf << 8)
    result = {
        'pgn': pgn,
        'priority': priority,
        'source_address': source_address,
        'pdu_format': pf,
        'pdu_specific': ps,
        'is_pdu_broadcast': is_pdu_broadcast,
    }
    # For PDU1 (destination-specific) messages, PS is the destination address
    if not is_pdu_broadcast:
        result['destination_address'] = ps
    return result


def _sanitize_identifier(name, fallback='X'):
    """Turn arbitrary DBC text into a clean, valid C++ identifier.

    Signal names can carry spaces and punctuation (e.g. cantools surfaces the DBC
    ``SystemSignalLongSymbol`` attribute as the signal name: "Accelerator Pedal 1
    Low Idle Switch"), and choice labels are free-form prose. Replace every run of
    non-alphanumeric characters with a single underscore, strip leading/trailing
    underscores, fall back when nothing is left, and prefix an underscore when the
    result would otherwise start with a digit ("1000 ms" -> "_1000_ms").
    """
    name = re.sub(r'[^0-9a-zA-Z]+', '_', name).strip('_')
    if not name:
        name = fallback
    if name[0].isdigit():
        name = '_' + name
    return name


def _escape_c_string(text):
    """Escape a DBC label so it is safe inside a C string literal."""
    return text.replace('\\', '\\\\').replace('"', '\\"')


def build_signal_enums(message, cg_message, used_enum_names):
    """Build C++ enum descriptors for every signal in a message that has VAL_ choices.

    Each enum is named <MessageName>_<SignalName> (top-level, prefixed to avoid
    collisions across messages) with enumerators derived from the DBC value table.
    ``used_enum_names`` is a set shared across the whole database used to keep enum
    type names globally unique.
    """
    enums = []
    for cg_signal in cg_message.cg_signals:
        choices = cg_signal.signal.choices
        if not choices:
            continue

        # Message/signal names can contain characters that are not valid in an
        # identifier (spaces from SystemSignalLongSymbol, etc.), so sanitize both
        # parts. Keep the type name globally unique as a final safeguard.
        enum_name = _sanitize_identifier(
            f'{_sanitize_identifier(message.name)}_{_sanitize_identifier(cg_signal.signal.name)}', fallback='Enum'
        )
        while enum_name in used_enum_names:
            enum_name += '_'
        used_enum_names.add(enum_name)

        # unique_choices gives {raw_int: UNIQUE_UPPER_IDENT}, already de-duplicated
        # and matching the identifiers cantools emits for its C ..._CHOICE #defines.
        unique = cg_signal.unique_choices
        raws = sorted(unique)

        # Sanitize each name into a valid enumerator, re-deduplicating in case two
        # names collapse to the same identifier (append the raw value, then '_').
        idents = {}
        used = set()
        for raw in raws:
            ident = _sanitize_identifier(unique[raw], fallback='VALUE')
            if ident in used:
                ident = f'{ident}_{raw}' if raw >= 0 else f'{ident}_n{-raw}'
                while ident in used:
                    ident += '_'
            used.add(ident)
            idents[raw] = ident

        # Choices on a scaled/float signal are still integer-raw; guard the type.
        if cg_signal.signal.conversion.is_float:
            underlying_type = f'int{cg_signal.type_length}_t'
        else:
            underlying_type = cg_signal.type_name

        enums.append({
            'name': enum_name,
            'signal_name': cg_signal.signal.name,
            'message_name': message.name,
            'underlying_type': underlying_type,
            'enumerators': [
                {
                    'ident': idents[raw],
                    'value': raw,
                    'label': _escape_c_string(str(choices[raw])),
                }
                for raw in raws
            ],
        })
    return enums


def generate_cpp_source(args):
    dbase = database.load_file(args.infile)
    database_name: str = args.database_name or camel_to_snake_case(args.infile.stem)
    filename_h = f'{database_name}.h'
    filename_c = f'{database_name}.c'
    filename_hpp = f'{database_name}.hpp'

    c_header, c_source, _, _ = generate_c_source(
        dbase,
        database_name,
        filename_h,
        filename_c,
        f'{database_name}_fuzzer.c',
    )

    outdir = args.output_directory
    outdir.mkdir(parents=True, exist_ok=True)
    with (outdir / filename_h).open('w') as f:
        f.write(c_header)
    with (outdir / filename_c).open('w') as f:
        f.write(c_source)
    print(f'Successfully generated C source files: {outdir / filename_h}, {outdir / filename_c}')

    jinja_env = Environment(trim_blocks=True, lstrip_blocks=True)

    template_path = importlib.resources.files('dbc_gen_cpp.templates')
    with template_path.joinpath('can.hpp.j2').open('r') as f:
        hpp_template_src = f.read()
    hpp_template = jinja_env.from_string(hpp_template_src)

    message_types = []
    signal_enums = []
    used_enum_names = set()
    for message in dbase.messages:
        cg_message = CodeGenMessage(message)
        signal_enums.extend(build_signal_enums(message, cg_message, used_enum_names))

        msg_dict = {
            'name': message.name,
            'struct_name': f'{database_name}_{cg_message.snake_name}',
            'can_id': message.frame_id,
            'is_extended_frame': message.is_extended_frame,
            'protocol': message.protocol,
            'data_length': message.length,
            'signals': [
                {
                    'name': cg_signal.snake_name,
                    'type_name': cg_signal.type_name,
                }
                for cg_signal in cg_message.cg_signals
            ],
        }
        if message.protocol == 'j1939':
            msg_dict['j1939'] = parse_j1939_id(message.frame_id)
        message_types.append(msg_dict)
    hpp_src = hpp_template.render(
        library_name=database_name,
        messages=message_types,
        enums=signal_enums,
        c_header=filename_h,
    )

    with (outdir / filename_hpp).open('w') as f:
        f.write(hpp_src)

    print(f'Successfully generated C++ source files: {outdir / filename_hpp}')


def main():
    parser = argparse.ArgumentParser('dbc_gen_cpp', description='Generate C++ code from DBC files.')
    parser.add_argument('infile', type=Path, help='Path to the DBC file.')
    parser.add_argument('-o', '--output-directory', type=Path, help='Directory to save the generated sources.')
    parser.add_argument('-n', '--database-name', type=str, default=None)
    args = parser.parse_args()
    generate_cpp_source(args)


if __name__ == '__main__':
    main()

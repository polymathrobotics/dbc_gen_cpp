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
    """Turn arbitrary DBC text (names, labels) into a valid C++ identifier."""
    name = re.sub(r'[^0-9a-zA-Z]+', '_', name).strip('_')
    if not name:
        name = fallback
    if name[0].isdigit():
        name = '_' + name
    return name


def _escape_c_string(text):
    """Escape a DBC label so it is safe inside a C string literal."""
    return text.replace('\\', '\\\\').replace('"', '\\"')


def _enum_underlying_type(sorted_values):
    """Smallest stdint type spanning the choice values.

    Driven by the values, not the signal's declared type, so float-typed signals
    still get an integral base and negatives/high-bit flags always fit.
    """
    low, high = sorted_values[0], sorted_values[-1]
    signed = low < 0
    for bits in (8, 16, 32, 64):
        if signed:
            if -(1 << (bits - 1)) <= low and high <= (1 << (bits - 1)) - 1:
                return f'int{bits}_t'
        elif high <= (1 << bits) - 1:
            return f'uint{bits}_t'
    return 'int64_t' if signed else 'uint64_t'


def build_signal_enums(message, cg_message, used_enum_names):
    """Build an <Message>_<Signal> enum descriptor per signal with VAL_ choices."""
    enums = []
    for cg_signal in cg_message.cg_signals:
        choices = cg_signal.signal.choices
        if not choices:
            continue

        descriptor = f'{message.name}.{cg_signal.signal.name}'
        enum_name = _sanitize_identifier(
            f'{_sanitize_identifier(message.name)}_{_sanitize_identifier(cg_signal.signal.name)}', fallback='Enum'
        )
        # Fail loudly for identical names
        if enum_name in used_enum_names:
            raise ValueError(
                f"Generated enum name '{enum_name}' collides between "
                f"'{used_enum_names[enum_name]}' and '{descriptor}'. Rename one of the "
                f'DBC signals (or its SystemSignalLongSymbol) so the sanitized '
                f'<Message>_<Signal> names are unique.'
            )
        used_enum_names[enum_name] = descriptor

        # De-duplicated UPPER_SNAKE names, matching cantools' C ..._CHOICE #defines.
        choice_name_by_value = cg_signal.unique_choices
        sorted_values = sorted(choice_name_by_value)

        enumerator_ident_by_value = {}
        used_idents = set()
        for raw_value in sorted_values:
            enumerator_ident = _sanitize_identifier(choice_name_by_value[raw_value], fallback='VALUE')
            if enumerator_ident in used_idents:
                # Repeated label (e.g. two "Reserved"): suffix the value to disambiguate.
                enumerator_ident = (
                    f'{enumerator_ident}_{raw_value}' if raw_value >= 0 else f'{enumerator_ident}_n{-raw_value}'
                )
            if enumerator_ident in used_idents:
                raise ValueError(
                    f"Enumerator '{enumerator_ident}' collides in enum '{enum_name}'. Two DBC "
                    f'choices sanitize to the same C++ identifier; rename one.'
                )
            used_idents.add(enumerator_ident)
            enumerator_ident_by_value[raw_value] = enumerator_ident

        enums.append({
            'name': enum_name,
            'signal_name': cg_signal.signal.name,
            'message_name': message.name,
            'underlying_type': _enum_underlying_type(sorted_values),
            'enumerators': [
                {
                    'ident': enumerator_ident_by_value[raw_value],
                    'value': raw_value,
                    'label': _escape_c_string(str(choices[raw_value])),
                }
                for raw_value in sorted_values
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
    used_enum_names = {}
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

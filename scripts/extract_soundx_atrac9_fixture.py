#!/usr/bin/env python3
"""Extract verified consecutive ATRAC9 superframes from a SoundX bank in a CPAK archive."""
from __future__ import annotations

import argparse
import hashlib
import mmap
from pathlib import Path
import struct


CPAK_RECORD_OFFSET = 0x14
CPAK_RECORD_SIZE = 0x218
CPAK_NAME_OFFSET = 0x14
CPAK_NAME_SIZE = 0x100
MAX_CPAK_RECORDS = 1_000_000
MAX_WAVE_ENTRIES = 100_000
CHUNK_SEARCH_LIMIT = 1 << 20


class FormatError(ValueError):
    pass


def checked_end(offset: int, size: int, limit: int, label: str) -> int:
    if offset < 0 or size < 0 or offset > limit or size > limit - offset:
        raise FormatError(f"{label} range exceeds container")
    return offset + size


def read_u32(data: mmap.mmap, offset: int, limit: int, label: str) -> int:
    checked_end(offset, 4, limit, label)
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data: mmap.mmap, offset: int, limit: int, label: str) -> int:
    checked_end(offset, 8, limit, label)
    return struct.unpack_from("<Q", data, offset)[0]


def fnv1a64(data: bytes) -> int:
    value = 1_469_598_103_934_665_603
    for byte in data:
        value = ((value ^ byte) * 1_099_511_628_211) & 0xFFFFFFFFFFFFFFFF
    return value


def parse_config(text: str) -> bytes:
    try:
        config = bytes.fromhex(text)
    except ValueError as error:
        raise FormatError("config must contain exactly eight hexadecimal digits") from error
    if len(config) != 4 or config[0] != 0xFE:
        raise FormatError("config must contain four ATRAC9 bytes in canonical order")
    return config


def find_cpak_member(data: mmap.mmap, member_name: str) -> tuple[int, int, int]:
    if len(data) < CPAK_RECORD_OFFSET or data[:4] != b"CPAK":
        raise FormatError("input is not a CPAK archive")
    count = read_u32(data, 4, len(data), "CPAK record count")
    if count > MAX_CPAK_RECORDS:
        raise FormatError("CPAK record count exceeds safety limit")
    checked_end(CPAK_RECORD_OFFSET, count * CPAK_RECORD_SIZE, len(data), "CPAK table")

    matches: list[tuple[int, int, int]] = []
    for index in range(count):
        record = CPAK_RECORD_OFFSET + index * CPAK_RECORD_SIZE
        member_offset = read_u64(data, record, len(data), "CPAK member offset")
        member_size = read_u64(data, record + 8, len(data), "CPAK member size")
        checked_end(member_offset, member_size, len(data), "CPAK member")
        name_start = record + CPAK_NAME_OFFSET
        name_raw = data[name_start:name_start + CPAK_NAME_SIZE].split(b"\0", 1)[0]
        try:
            name = name_raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise FormatError(f"CPAK record {index} has invalid UTF-8 name") from error
        if name == member_name:
            matches.append((index, member_offset, member_size))
    if len(matches) != 1:
        raise FormatError(f"expected one CPAK member named {member_name!r}, found {len(matches)}")
    return matches[0]


def find_marker(data: mmap.mmap, marker: bytes, start: int, end: int, label: str) -> int:
    position = data.find(marker, start, min(end, start + CHUNK_SEARCH_LIMIT))
    if position < 0:
        raise FormatError(f"{label} marker not found in SoundX metadata")
    return position


def locate_atrac9_payload(
    data: mmap.mmap,
    member_offset: int,
    member_size: int,
    config: bytes,
    channels: int,
    segment_size: int,
) -> dict[str, int]:
    member_end = checked_end(member_offset, member_size, len(data), "SoundX member")
    if data[member_offset:member_offset + 4] != b"SXDF":
        raise FormatError("CPAK member does not start with SXDF")

    wave = find_marker(data, b"WAVE", member_offset, member_end, "WAVE")
    count = read_u32(data, wave + 12, member_end, "WAVE entry count")
    if count == 0 or count > MAX_WAVE_ENTRIES:
        raise FormatError("WAVE entry count is invalid")
    checked_end(wave + 16, count * 4, member_end, "WAVE offset table")

    stored_config = config[::-1]
    matches: list[tuple[int, int, int]] = []
    for index in range(count):
        relative = read_u32(data, wave + 16 + index * 4, member_end, "WAVE entry offset")
        entry = wave + 16 + relative
        checked_end(entry, 0x24, member_end, "WAVE entry")
        if data[entry + 4] != 0x42 or data[entry + 5] != channels:
            continue
        if read_u32(data, entry + 8, member_end, "WAVE sample rate") != 48_000:
            continue
        if read_u32(data, entry + 0x20, member_end, "WAVE compressed size") != segment_size:
            continue

        config_offset = -1
        search_end = min(entry + 0x60, member_end)
        for candidate in range(entry + 0x28, search_end - 3, 4):
            if data[candidate:candidate + 4] == stored_config:
                config_offset = candidate
                break
        if config_offset >= 0:
            matches.append((index, entry, config_offset))
    if len(matches) != 1:
        raise FormatError(f"expected one matching ATRAC9 WAVE entry, found {len(matches)}")

    entry_index, entry, config_offset = matches[0]
    data_chunk = find_marker(data, b"DATA", wave + 4, member_end, "DATA")
    chunk_size = read_u32(data, data_chunk + 4, member_end, "DATA chunk size")
    if chunk_size != segment_size + 8:
        raise FormatError("DATA chunk size does not match WAVE compressed size")
    payload = data_chunk + 16
    checked_end(payload, segment_size, member_end, "ATRAC9 payload")
    return {
        "wave_offset": wave,
        "wave_entry_index": entry_index,
        "wave_entry_offset": entry,
        "config_offset": config_offset,
        "data_offset": data_chunk,
        "payload_offset": payload,
    }


def extract(args: argparse.Namespace) -> None:
    archive = args.archive.resolve()
    output = args.output.resolve()
    if archive == output:
        raise FormatError("output must differ from source archive")
    config = parse_config(args.config)
    expected_fingerprint = int(args.expected_fingerprint, 16)
    if not 0 <= expected_fingerprint <= 0xFFFFFFFFFFFFFFFF:
        raise FormatError("expected fingerprint exceeds 64 bits")
    if args.superframe_size <= 0 or args.segment_size % args.superframe_size:
        raise FormatError("segment size must contain complete superframes")
    if args.superframes <= 0:
        raise FormatError("superframes must be positive")
    extract_size = args.superframe_size * args.superframes
    if extract_size > args.segment_size:
        raise FormatError("requested superframes exceed ATRAC9 segment")

    with archive.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        member_index, member_offset, member_size = find_cpak_member(data, args.member)
        locations = locate_atrac9_payload(
            data, member_offset, member_size, config, args.channels, args.segment_size
        )
        payload_offset = locations["payload_offset"]
        fingerprint = fnv1a64(data[payload_offset:payload_offset + 32])
        if fingerprint != expected_fingerprint:
            raise FormatError(
                f"payload fingerprint mismatch: {fingerprint:016x} != {expected_fingerprint:016x}"
            )
        checked_end(payload_offset, extract_size, len(data), "ATRAC9 fixture")
        fixture = bytes(data[payload_offset:payload_offset + extract_size])

    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with output.open("xb") as target:
            target.write(fixture)
    except FileExistsError as error:
        raise FormatError("output already exists; refusing to overwrite it") from error

    print(f"archive={archive}")
    print(f"member={args.member}")
    print(f"member_index={member_index}")
    print(f"member_offset=0x{member_offset:X}")
    print(f"member_size={member_size}")
    print(f"wave_offset=0x{locations['wave_offset']:X}")
    print(f"wave_entry_index={locations['wave_entry_index']}")
    print(f"wave_entry_offset=0x{locations['wave_entry_offset']:X}")
    print(f"config_offset=0x{locations['config_offset']:X}")
    print(f"data_offset=0x{locations['data_offset']:X}")
    print(f"payload_offset=0x{locations['payload_offset']:X}")
    print(f"segment_size={args.segment_size}")
    print(f"superframe_size={args.superframe_size}")
    print(f"superframes_extracted={args.superframes}")
    print(f"fixture_size={len(fixture)}")
    print(f"config={config.hex().upper()}")
    print(f"fingerprint32_fnv1a64={fingerprint:016x}")
    print(f"fixture_sha256={hashlib.sha256(fixture).hexdigest()}")
    print(f"output={output}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--member", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--config", default="FE7A17F0")
    parser.add_argument("--channels", default=4, type=int)
    parser.add_argument("--segment-size", default=1_447_680, type=int)
    parser.add_argument("--superframe-size", default=768, type=int)
    parser.add_argument("--superframes", default=1, type=int)
    parser.add_argument("--expected-fingerprint", default="3ee686a13d32bab6")
    args = parser.parse_args()
    try:
        extract(args)
    except (FormatError, OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

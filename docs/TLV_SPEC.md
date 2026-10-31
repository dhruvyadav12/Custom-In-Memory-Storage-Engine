# Type-Length-Value (TLV) Protocol Specification

This document details the binary wire protocol and snapshot serialization format used by the Custom In-Memory Storage Engine.

---

## 1. Packet Wire Layout

Every TLV message begins with a 6-byte header followed by variable-length value bytes:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Magic (0xA5) |   Type (1B)   |        Length (4 Bytes)       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Length (cont.)          |        Value Payload ...      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        ... Value Payload                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Fields:
1. **Magic Byte (1 Byte)**: `0xA5`
   - Identifies incoming data as TLV binary protocol.
   - Allows automatic protocol switching: if the first byte is `0xA5`, the server decodes TLV; otherwise, it treats it as ASCII/RESP text protocol.
2. **Type Byte (1 Byte)**:
   - Describes the data format of the payload.
3. **Length (4 Bytes)**:
   - Big-endian (`uint32_t`) representing the byte count of the `Value` field.
4. **Value (N Bytes)**:
   - Raw binary or nested serialized payload.

---

## 2. Type Identifiers

| Type Code | Name | Description |
|-----------|------|-------------|
| `0x01` | `CMD_REQUEST` | Client request containing command arguments list |
| `0x02` | `RESP_OK` | Success status string (e.g. `"OK"`, `"PONG"`) |
| `0x03` | `RESP_ERR` | Error message string |
| `0x04` | `RESP_INT` | 64-bit signed integer (8 bytes, big-endian) |
| `0x05` | `RESP_STR` | String / binary payload |
| `0x06` | `RESP_ARRAY` | Array of strings / nested elements |
| `0x07` | `RESP_NIL` | Null / key not found indicator (Length = 0) |
| `0x08` | `RESP_MAP` | Map of key-value pairs (Hashes) |
| `0x09` | `RESP_AVL_RANGE` | AVL Tree range query results (member, score pairs) |
| `0x10` | `DATA_STRING` | Snapshot string record |
| `0x11` | `DATA_LIST` | Snapshot list record |
| `0x12` | `DATA_SET` | Snapshot set record |
| `0x13` | `DATA_HASH` | Snapshot hash record |
| `0x14` | `DATA_AVL` | Snapshot AVL sorted set record |

---

## 3. String List Encoding

When encoding a list of strings (such as in `CMD_REQUEST` or `RESP_ARRAY`):
Each string is preceded by a 4-byte big-endian length prefix:
```
[Len1: 4B][Str1 Bytes...][Len2: 4B][Str2 Bytes...]...
```

### Example: Encoding `SET mykey hello`
- Arguments: `["SET", "mykey", "hello"]`
- Value Payload:
  - `0x00 0x00 0x00 0x03` + `"SET"` (7 bytes)
  - `0x00 0x00 0x00 0x05` + `"mykey"` (9 bytes)
  - `0x00 0x00 0x00 0x05` + `"hello"` (9 bytes)
- Total Payload = 25 bytes
- Full TLV Packet:
  - Magic: `0xA5`
  - Type: `0x01` (`CMD_REQUEST`)
  - Length: `0x00 0x00 0x00 0x19` (25)
  - Payload: 25 bytes

---

## 4. Snapshot File Layout (`dump.tlv`)

The persistence file format uses a structured header followed by consecutive TLV records:
1. **Magic**: `0x544C5631` (ASCII `"TLV1"`, 4 bytes)
2. **Version**: `0x00000001` (4 bytes)
3. **Creation Timestamp**: Unix timestamp in milliseconds (8 bytes big-endian)
4. **Records**: For each entry:
   - `Key TLV`: Type `RESP_STR`
   - `Value TLV`: Type `DATA_STRING`, `DATA_LIST`, `DATA_SET`, `DATA_HASH`, or `DATA_AVL`
   - `TTL TLV`: Type `RESP_INT` (Remaining TTL in ms, or 0 if persistent)

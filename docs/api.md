# ring_buf_dp API reference

Header: `#include <ring_buf_dp.h>`. All functions are safe to call from threads
and ISRs (they take a `k_spinlock` internally); none of them block.

Naming: *primary reader* is the normal consumer (`get*`), *secondary reader* is
the optional second read pointer (`peek*`).

## Lifecycle

| Function | Description |
|----------|-------------|
| `RING_BUF_DP_DECLARE(name, size)` | Defines static storage and a static instance. `size` is 1 to `RING_BUF_DP_MAX_SIZE`, checked at build time. |
| `int ring_buf_dp_init(rb, buf, size)` | Initializes an instance over your storage. Returns `-EINVAL` for NULL arguments or a bad size. |
| `void ring_buf_dp_reset(rb)` | Drops all data and outstanding claims. The secondary reader stays enabled if it was. |

## Copy API

| Function | Description |
|----------|-------------|
| `uint32_t ring_buf_dp_put(rb, data, len)` | Writes up to `len` bytes, returns the number written. Returns 0 while a put claim is outstanding. |
| `uint32_t ring_buf_dp_get(rb, data, len)` | Reads and consumes up to `len` bytes for the primary reader. Returns 0 while a get claim is outstanding. |

## Zero-copy (double pointer) API

| Function | Description |
|----------|-------------|
| `uint32_t ring_buf_dp_put_claim(rb, uint8_t **data, len)` | Claims up to `len` contiguous writable bytes and stores the start in `*data` (NULL if 0 bytes). |
| `int ring_buf_dp_put_finish(rb, len)` | Commits `len` bytes of the claim. `-EINVAL` if `len` is larger than the claim. `0` commits nothing and drops the claim. |
| `uint32_t ring_buf_dp_get_claim(rb, uint8_t **data, len)` | Claims up to `len` contiguous readable bytes for the primary reader. |
| `int ring_buf_dp_get_finish(rb, len)` | Consumes `len` bytes of the claim. `-EINVAL` if larger than the claim. |

Rules:

- A claim returns at most the bytes up to the end of the storage. A region that
  wraps needs two claim/finish passes.
- Claim sizes can be smaller than requested. Always use the return value.
- One claim per kind may be outstanding. A new claim of the same kind replaces
  the previous one.
- Finishing less than the claim releases the rest; the next claim starts right
  after the finished bytes.
- The pointer stays valid until the matching finish. Bytes in a claimed read
  region are not overwritten, and a claimed write region is not read.

## Secondary reader

| Function | Description |
|----------|-------------|
| `int ring_buf_dp_second_enable(rb)` | Starts the secondary reader at the primary read position. `-EALREADY` if enabled. |
| `void ring_buf_dp_second_disable(rb)` | Stops it. It no longer limits the free space. |
| `uint32_t ring_buf_dp_peek(rb, data, len)` | Copies and consumes up to `len` bytes for the secondary reader. Returns 0 if disabled or while a peek claim is outstanding. |
| `uint32_t ring_buf_dp_peek_claim(rb, uint8_t **data, len)` | Zero-copy variant for the secondary reader. Returns 0 if disabled. |
| `int ring_buf_dp_peek_finish(rb, len)` | Consumes `len` bytes of the peek claim. `-EINVAL` if larger. |

Consuming from one reader never changes the other reader's data. Free space is
`size - max(primary_used, secondary_used)`.

## Status

| Function | Description |
|----------|-------------|
| `uint32_t ring_buf_dp_size_get(rb)` | Storage size. |
| `uint32_t ring_buf_dp_size_used(rb)` | Bytes ready for the primary reader. |
| `uint32_t ring_buf_dp_second_size_used(rb)` | Bytes ready for the secondary reader, 0 if disabled. |
| `uint32_t ring_buf_dp_space_get(rb)` | Bytes that can be written. |
| `bool ring_buf_dp_is_empty(rb)` | No data for the primary reader. |
| `bool ring_buf_dp_is_full(rb)` | No space left to write. |

## Kconfig

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_RING_BUF_DP` | n | Enables the module. |
| `CONFIG_RING_BUF_DP_LOG_LEVEL_*` | INF | Module log level (needs `CONFIG_LOG`). Rejected operations are logged at debug level. |
| `CONFIG_RING_BUF_DP_SHELL` | n | Adds the `rbdp` shell commands (needs `CONFIG_SHELL`). |
| `CONFIG_RING_BUF_DP_SHELL_BUF_SIZE` | 64 | Size of the shell demo buffer. |

## Shell commands

| Command | Description |
|---------|-------------|
| `rbdp info` | Size, used, secondary used, space, empty/full. |
| `rbdp put <string>` | Copies the string in. |
| `rbdp get <n>` / `rbdp peek <n>` | Copies out and consumes n bytes (primary / secondary reader), with a hex dump. |
| `rbdp second <on\|off>` | Enables or disables the secondary reader. |
| `rbdp claim_put <n>` | Claims n bytes and fills them with an incrementing pattern. |
| `rbdp finish_put <n>` | Commits n bytes of the claim. |
| `rbdp claim_get <n>` / `rbdp finish_get <n>` | Claims and dumps / consumes (primary reader). |
| `rbdp peek_claim <n>` / `rbdp peek_finish <n>` | Same for the secondary reader. |
| `rbdp reset` | Drops all data. |

## Error codes

| Code | Meaning |
|------|---------|
| `-EINVAL` | Bad argument, or `*_finish()` larger than the claim. |
| `-EALREADY` | `ring_buf_dp_second_enable()` while already enabled. |

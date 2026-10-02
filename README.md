# ring_buf_dp

A Zephyr module providing a byte ring buffer with a **double pointer** feature:

- **Zero-copy access.** `put_claim()` / `get_claim()` / `peek_claim()` return a
  `uint8_t **` into the backing storage. Producers (DMA, ISR) and consumers work
  in place and commit with `*_finish()`.
- **Secondary reader.** A second, independent read pointer lets a logger or
  monitor follow the stream without taking bytes from the main consumer.

It also ships a sample application, a Ztest suite and `rbdp` shell commands.

## Layout

| Path | Content |
|------|---------|
| `include/ring_buf_dp.h` | Public API |
| `src/ring_buf_dp.c` | Implementation |
| `src/shell_ring_buf_dp.c` | `rbdp` shell commands |
| `samples/basic` | Producer / consumer / monitor sample with shell |
| `tests/unit` | Ztest suite (plain and shell variants) |
| `docs/` | [design](docs/design.md), [API](docs/api.md), [use cases](docs/use-cases.md) |

## Quick start

Add the module to your west manifest:

```yaml
- name: ring_buf_dp
  url: https://github.com/AnkyXCoder/ring_buf_dp
  revision: main
  path: modules/lib/ring_buf_dp
```

or point CMake at it with `list(APPEND ZEPHYR_EXTRA_MODULES <path>)` before
`find_package(Zephyr)`. Then enable it in `prj.conf`:

```ini
CONFIG_RING_BUF_DP=y
```

```c
#include <ring_buf_dp.h>

RING_BUF_DP_DECLARE(rb, 64);

/* producer: write in place */
uint8_t *p;
uint32_t n = ring_buf_dp_put_claim(&rb, &p, 16);
memcpy(p, "hello", MIN(n, 5));
ring_buf_dp_put_finish(&rb, MIN(n, 5));

/* consumer: read in place */
n = ring_buf_dp_get_claim(&rb, &p, 16);
process(p, n);
ring_buf_dp_get_finish(&rb, n);

/* optional second reader */
ring_buf_dp_second_enable(&rb);
n = ring_buf_dp_peek_claim(&rb, &p, 16);
log_bytes(p, n);
ring_buf_dp_peek_finish(&rb, n);
```

A claim never wraps, so data that crosses the end of the storage takes two
claim/finish passes; see [use cases](docs/use-cases.md).

## Configuration

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_RING_BUF_DP` | n | Enable the module |
| `CONFIG_RING_BUF_DP_LOG_LEVEL_*` | INF | Log level |
| `CONFIG_RING_BUF_DP_SHELL` | n | `rbdp` shell commands |
| `CONFIG_RING_BUF_DP_SHELL_BUF_SIZE` | 64 | Shell demo buffer size |

## Build and test

From a west workspace that contains this module (the module sets itself up via
`ZEPHYR_EXTRA_MODULES` in the sample and test projects):

```sh
# sample
west build -b native_sim samples/basic
west build -t run

# unit tests
west twister -T tests -p native_sim -p qemu_cortex_m3
```

The sample prints `SAMPLE PASSED` after 1024 bytes went through the producer,
consumer and monitor threads, then leaves the shell running:

```text
uart:~$ rbdp put hello
uart:~$ rbdp info
```

## Documentation

Diagrams in `docs/*.md` are PlantUML. Regenerate the images with:

```sh
python3 docs/render_diagrams.py
```

## License

Apache-2.0, see [LICENSE](LICENSE).

# ring_buf_dp design

`ring_buf_dp` is a byte ring buffer for Zephyr with a *double pointer* feature:

1. **Zero-copy claim/finish API.** `put_claim()`, `get_claim()` and `peek_claim()`
   hand out a `uint8_t **` that points straight into the backing storage, so a
   producer (DMA, UART ISR) or a consumer can work in place.
2. **Secondary reader.** A second, independent read pointer (`tail2`) lets a
   monitor or logger observe the same stream without stealing bytes from the
   primary consumer.

## Block diagram

```plantuml
@startuml
skinparam componentStyle rectangle
skinparam shadowing false
left to right direction

package "Application" {
  [Producer\n(ISR / thread)] as P
  [Primary Consumer\n(thread)] as C
  [Secondary Reader\n(monitor / logger)] as S
  [Shell user] as U
}

package "ring_buf_dp module" {
  [Public API\nring_buf_dp.h] as API
  package "Core (ring_buf_dp.c)" {
    [Copy API\nput / get / peek] as COPY
    [Zero-copy API\nclaim / finish\n(uint8_t **)] as ZC
    [Index & Space Manager\nhead, tail, tail2] as IDX
    [Lock\nk_spinlock] as LK
  }
  [Shell commands\nshell_ring_buf_dp.c] as SH
}

package "Zephyr" {
  [Kernel\nspinlock, threads] as K
  [Shell subsystem] as ZS
  [Logging] as LOG
}

database "Backing storage\nuint8_t buf[size]" as BUF

P --> API
C --> API
S --> API
U --> ZS
ZS --> SH
SH --> API
API --> COPY
API --> ZC
COPY --> IDX
ZC --> IDX
IDX --> LK
LK --> K
IDX --> BUF : data access
ZC ..> BUF : returns pointers\ninto buffer
COPY ..> LOG
@enduml
```

## Class diagram

The C API is modelled as classes: one struct plus groups of functions that
operate on it.

```plantuml
@startuml
skinparam shadowing false
skinparam classAttributeIconSize 0
hide empty members

class ring_buf_dp <<struct>> {
  - buf : uint8_t *
  - size : uint32_t
  - head : uint32_t
  - tail : uint32_t
  - tail2 : uint32_t
  - put_claimed : uint32_t
  - get_claimed : uint32_t
  - peek_claimed : uint32_t
  - second_enabled : bool
  - lock : struct k_spinlock
}

class "Lifecycle API" as LC <<functions>> {
  + ring_buf_dp_init(rb, buf, size) : int
  + ring_buf_dp_reset(rb) : void
  + RING_BUF_DP_DECLARE(name, size)
}

class "Copy API" as CP <<functions>> {
  + ring_buf_dp_put(rb, data, len) : uint32_t
  + ring_buf_dp_get(rb, data, len) : uint32_t
  + ring_buf_dp_peek(rb, data, len) : uint32_t   // secondary reader
}

class "Zero-copy API (double pointer)" as ZC <<functions>> {
  + ring_buf_dp_put_claim(rb, **data, len) : uint32_t
  + ring_buf_dp_put_finish(rb, len) : int
  + ring_buf_dp_get_claim(rb, **data, len) : uint32_t
  + ring_buf_dp_get_finish(rb, len) : int
}

class "Secondary reader API" as SR <<functions>> {
  + ring_buf_dp_second_enable(rb) : int
  + ring_buf_dp_second_disable(rb) : void
  + ring_buf_dp_peek_claim(rb, **data, len) : uint32_t
  + ring_buf_dp_peek_finish(rb, len) : int
}

class "Status API" as ST <<functions>> {
  + ring_buf_dp_size_get(rb) : uint32_t
  + ring_buf_dp_size_used(rb) : uint32_t
  + ring_buf_dp_second_size_used(rb) : uint32_t
  + ring_buf_dp_space_get(rb) : uint32_t
  + ring_buf_dp_is_empty(rb) : bool
  + ring_buf_dp_is_full(rb) : bool
}

class "Shell (rbdp)" as SHL <<module>> {
  info, put, get, peek
  claim_put, finish_put
  claim_get, finish_get
  reset
}

LC ..> ring_buf_dp : creates / resets
CP ..> ring_buf_dp : uses
ZC ..> ring_buf_dp : uses
SR ..> ring_buf_dp : uses
ST ..> ring_buf_dp : reads
SHL ..> CP
SHL ..> ZC
SHL ..> SR
SHL ..> ST

note right of ring_buf_dp
  Indices live in [0, 2*size).
  used  = dist(head, tail)
  used2 = dist(head, tail2)  (enabled only)
  space = size - max(used, used2)
  Writable space is bounded by the
  slowest enabled reader.
end note
@enduml
```

## Sequence diagrams

### Zero-copy write with wrap-around

```plantuml
@startuml
skinparam shadowing false
skinparam sequenceMessageAlign center
autonumber

participant "Producer" as P
participant "ring_buf_dp" as R
database "buf[]" as B

== Pass 1: contiguous segment up to buffer end ==
P -> R : put_claim(&ptr, 100)
R -> R : lock; compute contiguous space\n(end of buffer reached at 60)
R --> P : returns 60, ptr -> &buf[head % size]
P -> B : write 60 bytes via ptr
P -> R : put_finish(60)
R -> R : head += 60; clear claim

== Pass 2: wrapped segment ==
P -> R : put_claim(&ptr, 40)
R --> P : returns 40, ptr -> &buf[0]
P -> B : write 40 bytes via ptr
P -> R : put_finish(40)
R -> R : head += 40

== Error path ==
P -> R : put_finish(999)
R --> P : -EINVAL (more than claimed)
@enduml
```

### Primary consumer and secondary reader

```plantuml
@startuml
skinparam shadowing false
skinparam sequenceMessageAlign center
autonumber

participant "Producer" as P
participant "Primary\nConsumer" as C
participant "Secondary\nReader" as S
participant "ring_buf_dp" as R

S -> R : second_enable()
R -> R : tail2 = tail

P -> R : put(data, 32)
R --> P : 32 (head = 32)

S -> R : peek_claim(&p2, 32)
R --> S : 32, p2 -> &buf[tail2]
S -> S : inspect / log bytes
S -> R : peek_finish(32)
R -> R : tail2 = 32\n(space NOT freed, tail still 0)

C -> R : get_claim(&p1, 32)
R --> C : 32, p1 -> &buf[tail]
C -> C : process bytes
C -> R : get_finish(32)
R -> R : tail = 32\nfree space = size - (head - min(tail, tail2))

note over R
  Space is reclaimed only when BOTH
  readers have moved past the bytes.
end note
@enduml
```

### Shell session

```plantuml
@startuml
skinparam shadowing false
skinparam sequenceMessageAlign center
autonumber

actor User
participant "Zephyr Shell" as SH
participant "shell_ring_buf_dp" as CMD
participant "ring_buf_dp" as R

User -> SH : rbdp claim_put 8
SH -> CMD : cmd_claim_put(8)
CMD -> R : put_claim(&ptr, 8)
R --> CMD : 8
CMD --> User : "claimed 8 bytes"

User -> SH : rbdp finish_put 8
SH -> CMD : cmd_finish_put(8)
CMD -> R : put_finish(8)
R --> CMD : 0
CMD --> User : "committed, used=8"

User -> SH : rbdp peek 8
CMD -> R : peek(...)
CMD --> User : hex dump (secondary reader)

User -> SH : rbdp info
CMD -> R : size / used / space / tail / tail2
CMD --> User : status table
@enduml
```

## Design rules

| Topic | Rule |
|-------|------|
| Indices | `head`, `tail`, `tail2` are kept in `[0, 2*size)`. Distance is `(a - b) mod 2*size`, the buffer offset is `idx mod size`. This distinguishes full from empty and works for any `size` up to `RING_BUF_DP_MAX_SIZE`. |
| Space | `space = size - max(used, used2)`; `used2` counts only while the secondary reader is enabled. Space is reclaimed only after both readers have passed the bytes. |
| Claims | One outstanding claim per kind (put, get, peek). A new claim supersedes an unfinished claim of the same kind. A claim returns at most the contiguous bytes up to the end of the storage, so a wrapped region needs two claim/finish passes. |
| Finish | `*_finish(len)` fails with `-EINVAL` if `len` exceeds the claimed size. Finishing less than claimed releases the remainder. |
| Copy vs claim | While a put (get / peek) claim is outstanding, `put` (`get` / `peek`) returns 0 to avoid overlapping the claimed region. |
| Secondary reader | `second_enable()` starts it at the primary tail, so it sees all unread data. `second_disable()` removes it from the space calculation. `peek*()` consume from this reader only. |
| Concurrency | All index updates are done under a `k_spinlock`, so it is safe for ISR and thread. Data is copied or written through claimed pointers outside the lock, which is safe for one producer and one consumer per pointer. |

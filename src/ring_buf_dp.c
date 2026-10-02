/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <ring_buf_dp.h>

LOG_MODULE_REGISTER(ring_buf_dp, CONFIG_RING_BUF_DP_LOG_LEVEL);

static inline uint32_t idx_mod(const struct ring_buf_dp *rb)
{
	return 2U * rb->size;
}

static inline uint32_t idx_add(const struct ring_buf_dp *rb, uint32_t idx, uint32_t n)
{
	return (uint32_t)(((uint64_t)idx + n) % idx_mod(rb));
}

static inline uint32_t idx_dist(const struct ring_buf_dp *rb, uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a + idx_mod(rb) - b) % idx_mod(rb));
}

static inline uint32_t idx_off(const struct ring_buf_dp *rb, uint32_t idx)
{
	return idx % rb->size;
}

static inline uint32_t used_locked(const struct ring_buf_dp *rb)
{
	return idx_dist(rb, rb->head, rb->tail);
}

static inline uint32_t second_used_locked(const struct ring_buf_dp *rb)
{
	return rb->second_enabled ? idx_dist(rb, rb->head, rb->tail2) : 0U;
}

static inline uint32_t space_locked(const struct ring_buf_dp *rb)
{
	return rb->size - MAX(used_locked(rb), second_used_locked(rb));
}

int ring_buf_dp_init(struct ring_buf_dp *rb, uint8_t *buf, uint32_t size)
{
	if (rb == NULL || buf == NULL || size == 0U || size > RING_BUF_DP_MAX_SIZE) {
		return -EINVAL;
	}

	memset(rb, 0, sizeof(*rb));
	rb->buf = buf;
	rb->size = size;
	return 0;
}

void ring_buf_dp_reset(struct ring_buf_dp *rb)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);

	rb->head = 0U;
	rb->tail = 0U;
	rb->tail2 = 0U;
	rb->put_claimed = 0U;
	rb->get_claimed = 0U;
	rb->peek_claimed = 0U;
	k_spin_unlock(&rb->lock, key);
}

uint32_t ring_buf_dp_size_get(const struct ring_buf_dp *rb)
{
	return rb->size;
}

uint32_t ring_buf_dp_size_used(struct ring_buf_dp *rb)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t n = used_locked(rb);

	k_spin_unlock(&rb->lock, key);
	return n;
}

uint32_t ring_buf_dp_second_size_used(struct ring_buf_dp *rb)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t n = second_used_locked(rb);

	k_spin_unlock(&rb->lock, key);
	return n;
}

uint32_t ring_buf_dp_space_get(struct ring_buf_dp *rb)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t n = space_locked(rb);

	k_spin_unlock(&rb->lock, key);
	return n;
}

bool ring_buf_dp_is_empty(struct ring_buf_dp *rb)
{
	return ring_buf_dp_size_used(rb) == 0U;
}

bool ring_buf_dp_is_full(struct ring_buf_dp *rb)
{
	return ring_buf_dp_space_get(rb) == 0U;
}

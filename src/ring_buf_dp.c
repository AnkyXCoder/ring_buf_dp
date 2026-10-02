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

static void copy_in(const struct ring_buf_dp *rb, uint32_t idx, const uint8_t *src, uint32_t n)
{
	uint32_t off = idx_off(rb, idx);
	uint32_t first = MIN(n, rb->size - off);

	memcpy(&rb->buf[off], src, first);
	memcpy(rb->buf, src + first, n - first);
}

static void copy_out(const struct ring_buf_dp *rb, uint32_t idx, uint8_t *dst, uint32_t n)
{
	uint32_t off = idx_off(rb, idx);
	uint32_t first = MIN(n, rb->size - off);

	memcpy(dst, &rb->buf[off], first);
	memcpy(dst + first, rb->buf, n - first);
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

uint32_t ring_buf_dp_put(struct ring_buf_dp *rb, const uint8_t *data, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t n = 0U;

	if (rb->put_claimed == 0U) {
		n = MIN(len, space_locked(rb));
		copy_in(rb, rb->head, data, n);
		rb->head = idx_add(rb, rb->head, n);
	} else {
		LOG_DBG("put rejected, claim outstanding");
	}

	k_spin_unlock(&rb->lock, key);
	return n;
}

uint32_t ring_buf_dp_get(struct ring_buf_dp *rb, uint8_t *data, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t n = 0U;

	if (rb->get_claimed == 0U) {
		n = MIN(len, used_locked(rb));
		copy_out(rb, rb->tail, data, n);
		rb->tail = idx_add(rb, rb->tail, n);
	} else {
		LOG_DBG("get rejected, claim outstanding");
	}

	k_spin_unlock(&rb->lock, key);
	return n;
}

uint32_t ring_buf_dp_put_claim(struct ring_buf_dp *rb, uint8_t **data, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t off = idx_off(rb, rb->head);
	uint32_t n = MIN(MIN(len, space_locked(rb)), rb->size - off);

	*data = (n != 0U) ? &rb->buf[off] : NULL;
	rb->put_claimed = n;
	k_spin_unlock(&rb->lock, key);
	return n;
}

int ring_buf_dp_put_finish(struct ring_buf_dp *rb, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	int ret = 0;

	if (len > rb->put_claimed) {
		LOG_DBG("put_finish %u > claimed %u", len, rb->put_claimed);
		ret = -EINVAL;
	} else {
		rb->head = idx_add(rb, rb->head, len);
		rb->put_claimed = 0U;
	}

	k_spin_unlock(&rb->lock, key);
	return ret;
}

uint32_t ring_buf_dp_get_claim(struct ring_buf_dp *rb, uint8_t **data, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	uint32_t off = idx_off(rb, rb->tail);
	uint32_t n = MIN(MIN(len, used_locked(rb)), rb->size - off);

	*data = (n != 0U) ? &rb->buf[off] : NULL;
	rb->get_claimed = n;
	k_spin_unlock(&rb->lock, key);
	return n;
}

int ring_buf_dp_get_finish(struct ring_buf_dp *rb, uint32_t len)
{
	k_spinlock_key_t key = k_spin_lock(&rb->lock);
	int ret = 0;

	if (len > rb->get_claimed) {
		LOG_DBG("get_finish %u > claimed %u", len, rb->get_claimed);
		ret = -EINVAL;
	} else {
		rb->tail = idx_add(rb, rb->tail, len);
		rb->get_claimed = 0U;
	}

	k_spin_unlock(&rb->lock, key);
	return ret;
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

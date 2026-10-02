/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Byte ring buffer with zero-copy double pointer API and a secondary reader.
 */

#ifndef RING_BUF_DP_H_
#define RING_BUF_DP_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest supported buffer size in bytes (indices span 2 * size). */
#define RING_BUF_DP_MAX_SIZE 0x40000000U

/**
 * @brief Ring buffer instance. Treat all members as private.
 *
 * head, tail and tail2 are kept in [0, 2 * size).
 */
struct ring_buf_dp {
	uint8_t *buf;
	uint32_t size;
	uint32_t head;
	uint32_t tail;
	uint32_t tail2;
	uint32_t put_claimed;
	uint32_t get_claimed;
	uint32_t peek_claimed;
	bool second_enabled;
	struct k_spinlock lock;
};

/**
 * @brief Statically define a ring buffer and its storage.
 *
 * @param name Instance name.
 * @param sz   Storage size in bytes (1 .. RING_BUF_DP_MAX_SIZE).
 */
#define RING_BUF_DP_DECLARE(name, sz)                                                              \
	BUILD_ASSERT((sz) > 0 && (sz) <= RING_BUF_DP_MAX_SIZE, "invalid ring_buf_dp size");        \
	static uint8_t _ring_buf_dp_data_##name[sz];                                               \
	static struct ring_buf_dp name = {                                                         \
		.buf = _ring_buf_dp_data_##name,                                                   \
		.size = (sz),                                                                      \
	}

/**
 * @brief Initialize a ring buffer over caller provided storage.
 *
 * @retval 0 on success.
 * @retval -EINVAL if rb or buf is NULL or size is out of range.
 */
int ring_buf_dp_init(struct ring_buf_dp *rb, uint8_t *buf, uint32_t size);

/** @brief Drop all data and outstanding claims. The secondary reader stays enabled if it was. */
void ring_buf_dp_reset(struct ring_buf_dp *rb);

/**
 * @brief Copy data into the buffer.
 *
 * Writes as many bytes as fit. Returns 0 while a put claim is outstanding.
 *
 * @return Number of bytes written.
 */
uint32_t ring_buf_dp_put(struct ring_buf_dp *rb, const uint8_t *data, uint32_t len);

/**
 * @brief Copy data out of the buffer for the primary reader and consume it.
 *
 * Returns 0 while a get claim is outstanding.
 *
 * @return Number of bytes read.
 */
uint32_t ring_buf_dp_get(struct ring_buf_dp *rb, uint8_t *data, uint32_t len);

/** @brief Storage size in bytes. */
uint32_t ring_buf_dp_size_get(const struct ring_buf_dp *rb);

/** @brief Bytes available to the primary reader. */
uint32_t ring_buf_dp_size_used(struct ring_buf_dp *rb);

/** @brief Bytes available to the secondary reader, 0 if it is disabled. */
uint32_t ring_buf_dp_second_size_used(struct ring_buf_dp *rb);

/** @brief Bytes that can be written; bounded by the slowest enabled reader. */
uint32_t ring_buf_dp_space_get(struct ring_buf_dp *rb);

/** @brief True if the primary reader has no data. */
bool ring_buf_dp_is_empty(struct ring_buf_dp *rb);

/** @brief True if no space is left for writing. */
bool ring_buf_dp_is_full(struct ring_buf_dp *rb);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUF_DP_H_ */

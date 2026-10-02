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

/**
 * @brief Claim contiguous writable space inside the storage (zero-copy write).
 *
 * The region ends at the end of the storage, so a wrapped write needs two
 * claim/finish passes. A new claim supersedes an unfinished one.
 *
 * @param rb   Ring buffer.
 * @param data Set to the start of the claimed region, NULL if nothing is claimed.
 * @param len  Requested size in bytes.
 *
 * @return Number of bytes claimed, may be less than @p len.
 */
uint32_t ring_buf_dp_put_claim(struct ring_buf_dp *rb, uint8_t **data, uint32_t len);

/**
 * @brief Commit bytes written into a region from ring_buf_dp_put_claim().
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p len exceeds the claimed size.
 */
int ring_buf_dp_put_finish(struct ring_buf_dp *rb, uint32_t len);

/**
 * @brief Claim contiguous readable data for the primary reader (zero-copy read).
 *
 * @param rb   Ring buffer.
 * @param data Set to the start of the claimed region, NULL if nothing is claimed.
 * @param len  Requested size in bytes.
 *
 * @return Number of bytes claimed, may be less than @p len.
 */
uint32_t ring_buf_dp_get_claim(struct ring_buf_dp *rb, uint8_t **data, uint32_t len);

/**
 * @brief Consume bytes from a region obtained with ring_buf_dp_get_claim().
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p len exceeds the claimed size.
 */
int ring_buf_dp_get_finish(struct ring_buf_dp *rb, uint32_t len);

/**
 * @brief Enable the secondary reader, starting at the primary read position.
 *
 * While enabled, free space is bounded by the slowest of both readers.
 *
 * @retval 0 on success.
 * @retval -EALREADY if it is already enabled.
 */
int ring_buf_dp_second_enable(struct ring_buf_dp *rb);

/** @brief Disable the secondary reader; it no longer limits the free space. */
void ring_buf_dp_second_disable(struct ring_buf_dp *rb);

/**
 * @brief Copy data out for the secondary reader and consume it from that reader only.
 *
 * Returns 0 if the secondary reader is disabled or a peek claim is outstanding.
 *
 * @return Number of bytes read.
 */
uint32_t ring_buf_dp_peek(struct ring_buf_dp *rb, uint8_t *data, uint32_t len);

/**
 * @brief Claim contiguous readable data for the secondary reader (zero-copy).
 *
 * @return Number of bytes claimed, 0 if the secondary reader is disabled.
 */
uint32_t ring_buf_dp_peek_claim(struct ring_buf_dp *rb, uint8_t **data, uint32_t len);

/**
 * @brief Consume bytes from a region obtained with ring_buf_dp_peek_claim().
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p len exceeds the claimed size.
 */
int ring_buf_dp_peek_finish(struct ring_buf_dp *rb, uint32_t len);

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

/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ring_buf_dp.h>

#define BUF_SIZE 8

static uint8_t storage[BUF_SIZE];
static struct ring_buf_dp rb;

static void before(void *unused)
{
	ARG_UNUSED(unused);
	zassert_ok(ring_buf_dp_init(&rb, storage, sizeof(storage)));
}

ZTEST_SUITE(ring_buf_dp_basic, NULL, NULL, before, NULL, NULL);
ZTEST_SUITE(ring_buf_dp_claim, NULL, NULL, before, NULL, NULL);
ZTEST_SUITE(ring_buf_dp_second, NULL, NULL, before, NULL, NULL);
ZTEST_SUITE(ring_buf_dp_stress, NULL, NULL, NULL, NULL, NULL);

RING_BUF_DP_DECLARE(declared, 16);

ZTEST(ring_buf_dp_basic, test_init_validation)
{
	struct ring_buf_dp tmp;

	zassert_equal(ring_buf_dp_init(NULL, storage, 4), -EINVAL);
	zassert_equal(ring_buf_dp_init(&tmp, NULL, 4), -EINVAL);
	zassert_equal(ring_buf_dp_init(&tmp, storage, 0), -EINVAL);
	zassert_equal(ring_buf_dp_init(&tmp, storage, RING_BUF_DP_MAX_SIZE + 1), -EINVAL);
	zassert_ok(ring_buf_dp_init(&tmp, storage, 1));
}

ZTEST(ring_buf_dp_basic, test_declare_and_state)
{
	zassert_equal(ring_buf_dp_size_get(&declared), 16);
	zassert_true(ring_buf_dp_is_empty(&declared));
	zassert_false(ring_buf_dp_is_full(&declared));
	zassert_equal(ring_buf_dp_space_get(&declared), 16);
	zassert_equal(ring_buf_dp_size_used(&declared), 0);
}

ZTEST(ring_buf_dp_basic, test_put_get)
{
	uint8_t out[BUF_SIZE];

	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"abc", 3), 3);
	zassert_equal(ring_buf_dp_size_used(&rb), 3);
	zassert_equal(ring_buf_dp_space_get(&rb), BUF_SIZE - 3);
	zassert_equal(ring_buf_dp_get(&rb, out, 2), 2);
	zassert_mem_equal(out, "ab", 2);
	zassert_equal(ring_buf_dp_get(&rb, out, 8), 1);
	zassert_equal(out[0], 'c');
	zassert_true(ring_buf_dp_is_empty(&rb));
	zassert_equal(ring_buf_dp_get(&rb, out, 1), 0);
}

ZTEST(ring_buf_dp_basic, test_full_and_partial_put)
{
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"0123456789", 10), BUF_SIZE);
	zassert_true(ring_buf_dp_is_full(&rb));
	zassert_false(ring_buf_dp_is_empty(&rb));
	zassert_equal(ring_buf_dp_space_get(&rb), 0);
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"x", 1), 0);
}

ZTEST(ring_buf_dp_basic, test_wraparound_copy)
{
	uint8_t out[BUF_SIZE];

	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"abcdef", 6), 6);
	zassert_equal(ring_buf_dp_get(&rb, out, 5), 5);
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"ghijk", 5), 5);
	zassert_equal(ring_buf_dp_get(&rb, out, BUF_SIZE), 6);
	zassert_mem_equal(out, "fghijk", 6);
}

ZTEST(ring_buf_dp_basic, test_reset)
{
	uint8_t *p;

	ring_buf_dp_put(&rb, (const uint8_t *)"abc", 3);
	ring_buf_dp_put_claim(&rb, &p, 2);
	ring_buf_dp_reset(&rb);
	zassert_true(ring_buf_dp_is_empty(&rb));
	zassert_equal(ring_buf_dp_space_get(&rb), BUF_SIZE);
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"z", 1), 1);
}

ZTEST(ring_buf_dp_claim, test_put_claim_get_claim)
{
	uint8_t *p;

	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 5), 5);
	zassert_equal_ptr(p, &storage[0]);
	memcpy(p, "hello", 5);
	zassert_equal(ring_buf_dp_size_used(&rb), 0, "not visible before finish");
	zassert_ok(ring_buf_dp_put_finish(&rb, 5));
	zassert_equal(ring_buf_dp_size_used(&rb), 5);

	zassert_equal(ring_buf_dp_get_claim(&rb, &p, 8), 5);
	zassert_mem_equal(p, "hello", 5);
	zassert_equal(ring_buf_dp_size_used(&rb), 5, "not consumed before finish");
	zassert_ok(ring_buf_dp_get_finish(&rb, 5));
	zassert_true(ring_buf_dp_is_empty(&rb));
}

ZTEST(ring_buf_dp_claim, test_put_claim_wraps_in_two_passes)
{
	uint8_t *p;
	uint8_t out[BUF_SIZE];

	ring_buf_dp_put(&rb, (const uint8_t *)"123456", 6);
	ring_buf_dp_get(&rb, out, 6);

	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 6), 2, "up to the end of storage");
	zassert_equal_ptr(p, &storage[6]);
	memcpy(p, "ab", 2);
	zassert_ok(ring_buf_dp_put_finish(&rb, 2));

	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 4), 4);
	zassert_equal_ptr(p, &storage[0]);
	memcpy(p, "cdef", 4);
	zassert_ok(ring_buf_dp_put_finish(&rb, 4));

	zassert_equal(ring_buf_dp_get(&rb, out, BUF_SIZE), 6);
	zassert_mem_equal(out, "abcdef", 6);
}

ZTEST(ring_buf_dp_claim, test_get_claim_wraps_in_two_passes)
{
	uint8_t *p;
	uint8_t out[BUF_SIZE];

	ring_buf_dp_put(&rb, (const uint8_t *)"123456", 6);
	ring_buf_dp_get(&rb, out, 6);
	ring_buf_dp_put(&rb, (const uint8_t *)"abcdef", 6);

	zassert_equal(ring_buf_dp_get_claim(&rb, &p, 6), 2);
	zassert_mem_equal(p, "ab", 2);
	zassert_ok(ring_buf_dp_get_finish(&rb, 2));
	zassert_equal(ring_buf_dp_get_claim(&rb, &p, 6), 4);
	zassert_equal_ptr(p, &storage[0]);
	zassert_mem_equal(p, "cdef", 4);
	zassert_ok(ring_buf_dp_get_finish(&rb, 4));
	zassert_true(ring_buf_dp_is_empty(&rb));
}

ZTEST(ring_buf_dp_claim, test_finish_errors_and_partial_finish)
{
	uint8_t *p;

	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 4), 4);
	zassert_equal(ring_buf_dp_put_finish(&rb, 5), -EINVAL);
	zassert_ok(ring_buf_dp_put_finish(&rb, 2), "finishing less releases the rest");
	zassert_equal(ring_buf_dp_size_used(&rb), 2);
	zassert_equal(ring_buf_dp_put_finish(&rb, 1), -EINVAL, "claim was consumed");

	zassert_equal(ring_buf_dp_get_claim(&rb, &p, 4), 2);
	zassert_equal(ring_buf_dp_get_finish(&rb, 3), -EINVAL);
	zassert_ok(ring_buf_dp_get_finish(&rb, 0), "zero cancels the claim");
	zassert_equal(ring_buf_dp_size_used(&rb), 2);
}

ZTEST(ring_buf_dp_claim, test_empty_and_full_claims_return_null)
{
	uint8_t *p = (uint8_t *)1;

	zassert_equal(ring_buf_dp_get_claim(&rb, &p, 4), 0);
	zassert_is_null(p);

	ring_buf_dp_put(&rb, (const uint8_t *)"01234567", 8);
	p = (uint8_t *)1;
	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 4), 0);
	zassert_is_null(p);
}

ZTEST(ring_buf_dp_claim, test_new_claim_supersedes_old)
{
	uint8_t *p;

	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 6), 6);
	zassert_equal(ring_buf_dp_put_claim(&rb, &p, 2), 2);
	zassert_equal(ring_buf_dp_put_finish(&rb, 3), -EINVAL);
	zassert_ok(ring_buf_dp_put_finish(&rb, 2));
}

ZTEST(ring_buf_dp_claim, test_copy_rejected_while_claimed)
{
	uint8_t *p;
	uint8_t out[4];

	ring_buf_dp_put(&rb, (const uint8_t *)"abcd", 4);
	ring_buf_dp_put_claim(&rb, &p, 2);
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"x", 1), 0);
	ring_buf_dp_put_finish(&rb, 0);
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"x", 1), 1);

	ring_buf_dp_get_claim(&rb, &p, 2);
	zassert_equal(ring_buf_dp_get(&rb, out, 2), 0);
	ring_buf_dp_get_finish(&rb, 2);
	zassert_equal(ring_buf_dp_get(&rb, out, 2), 2);
}

ZTEST(ring_buf_dp_claim, test_claimed_read_region_is_not_overwritten)
{
	uint8_t *rp;
	uint8_t *wp;

	ring_buf_dp_put(&rb, (const uint8_t *)"abcdefgh", 8);
	zassert_equal(ring_buf_dp_get_claim(&rb, &rp, 4), 4);
	zassert_equal(ring_buf_dp_put_claim(&rb, &wp, 4), 0, "no space until get_finish");
	zassert_ok(ring_buf_dp_get_finish(&rb, 4));
	zassert_equal(ring_buf_dp_put_claim(&rb, &wp, 4), 4);
}

ZTEST(ring_buf_dp_second, test_enable_disable)
{
	zassert_ok(ring_buf_dp_second_enable(&rb));
	zassert_equal(ring_buf_dp_second_enable(&rb), -EALREADY);
	ring_buf_dp_second_disable(&rb);
	zassert_ok(ring_buf_dp_second_enable(&rb));
}

ZTEST(ring_buf_dp_second, test_disabled_reader_returns_nothing)
{
	uint8_t out[4];
	uint8_t *p;

	ring_buf_dp_put(&rb, (const uint8_t *)"abcd", 4);
	zassert_equal(ring_buf_dp_peek(&rb, out, 4), 0);
	zassert_equal(ring_buf_dp_peek_claim(&rb, &p, 4), 0);
	zassert_equal(ring_buf_dp_second_size_used(&rb), 0);
}

ZTEST(ring_buf_dp_second, test_starts_at_primary_tail)
{
	uint8_t out[BUF_SIZE];

	ring_buf_dp_put(&rb, (const uint8_t *)"abcdef", 6);
	ring_buf_dp_get(&rb, out, 2);
	zassert_ok(ring_buf_dp_second_enable(&rb));
	zassert_equal(ring_buf_dp_second_size_used(&rb), 4);
	zassert_equal(ring_buf_dp_peek(&rb, out, 8), 4);
	zassert_mem_equal(out, "cdef", 4);
}

ZTEST(ring_buf_dp_second, test_readers_are_independent)
{
	uint8_t a[BUF_SIZE];
	uint8_t b[BUF_SIZE];

	ring_buf_dp_second_enable(&rb);
	ring_buf_dp_put(&rb, (const uint8_t *)"abcd", 4);

	zassert_equal(ring_buf_dp_peek(&rb, b, 2), 2);
	zassert_mem_equal(b, "ab", 2);
	zassert_equal(ring_buf_dp_size_used(&rb), 4, "primary unaffected");

	zassert_equal(ring_buf_dp_get(&rb, a, 4), 4);
	zassert_mem_equal(a, "abcd", 4);
	zassert_equal(ring_buf_dp_second_size_used(&rb), 2, "secondary unaffected");
	zassert_equal(ring_buf_dp_peek(&rb, b, 4), 2);
	zassert_mem_equal(b, "cd", 2);
}

ZTEST(ring_buf_dp_second, test_space_bounded_by_slowest_reader)
{
	uint8_t out[BUF_SIZE];

	ring_buf_dp_second_enable(&rb);
	ring_buf_dp_put(&rb, (const uint8_t *)"01234567", 8);
	ring_buf_dp_get(&rb, out, 8);
	zassert_equal(ring_buf_dp_space_get(&rb), 0, "secondary still holds the data");
	zassert_true(ring_buf_dp_is_full(&rb));
	zassert_equal(ring_buf_dp_put(&rb, (const uint8_t *)"x", 1), 0);

	ring_buf_dp_peek(&rb, out, 3);
	zassert_equal(ring_buf_dp_space_get(&rb), 3);

	ring_buf_dp_second_disable(&rb);
	zassert_equal(ring_buf_dp_space_get(&rb), 8);
}

ZTEST(ring_buf_dp_second, test_peek_claim_finish_wraps)
{
	uint8_t *p;
	uint8_t out[BUF_SIZE];

	ring_buf_dp_second_enable(&rb);
	ring_buf_dp_put(&rb, (const uint8_t *)"123456", 6);
	ring_buf_dp_get(&rb, out, 6);
	ring_buf_dp_peek(&rb, out, 6);
	ring_buf_dp_put(&rb, (const uint8_t *)"abcdef", 6);

	zassert_equal(ring_buf_dp_peek_claim(&rb, &p, 6), 2);
	zassert_mem_equal(p, "ab", 2);
	zassert_equal(ring_buf_dp_peek_finish(&rb, 3), -EINVAL);
	zassert_ok(ring_buf_dp_peek_finish(&rb, 2));
	zassert_equal(ring_buf_dp_peek_claim(&rb, &p, 6), 4);
	zassert_mem_equal(p, "cdef", 4);
	zassert_equal(ring_buf_dp_peek(&rb, out, 1), 0, "copy rejected while claimed");
	zassert_ok(ring_buf_dp_peek_finish(&rb, 4));
	zassert_equal(ring_buf_dp_second_size_used(&rb), 0);
}

#define MODEL_SIZE 7
#define FUZZ_OPS   20000

static inline uint8_t stream_byte(uint32_t pos)
{
	return (uint8_t)(pos * 7U + 3U);
}

ZTEST(ring_buf_dp_stress, test_randomized_against_model)
{
	static uint8_t mem[MODEL_SIZE];
	static struct ring_buf_dp r;
	uint32_t seed = 12345U;
	uint32_t wr = 0U;
	uint32_t rd1 = 0U;
	uint32_t rd2 = 0U;
	bool second = false;
	uint8_t tmp[MODEL_SIZE + 1];
	uint8_t *p;

	zassert_ok(ring_buf_dp_init(&r, mem, sizeof(mem)));

	for (int i = 0; i < FUZZ_OPS; i++) {
		uint32_t used = wr - rd1;
		uint32_t used2 = second ? wr - rd2 : 0U;
		uint32_t space = MODEL_SIZE - MAX(used, used2);
		uint32_t len;
		uint32_t n;

		seed = seed * 1103515245U + 12345U;
		len = (seed >> 16) % (MODEL_SIZE + 2);

		switch ((seed >> 8) % 8) {
		case 0:
			for (uint32_t k = 0; k < len; k++) {
				tmp[k % sizeof(tmp)] = stream_byte(wr + k);
			}
			len = MIN(len, sizeof(tmp));
			n = ring_buf_dp_put(&r, tmp, len);
			zassert_equal(n, MIN(len, space));
			wr += n;
			break;
		case 1:
			n = ring_buf_dp_put_claim(&r, &p, len);
			zassert_true(n <= MIN(len, space));
			for (uint32_t k = 0; k < n; k++) {
				p[k] = stream_byte(wr + k);
			}
			zassert_ok(ring_buf_dp_put_finish(&r, n));
			wr += n;
			break;
		case 2:
			len = MIN(len, sizeof(tmp));
			n = ring_buf_dp_get(&r, tmp, len);
			zassert_equal(n, MIN(len, used));
			for (uint32_t k = 0; k < n; k++) {
				zassert_equal(tmp[k], stream_byte(rd1 + k));
			}
			rd1 += n;
			break;
		case 3:
			n = ring_buf_dp_get_claim(&r, &p, len);
			zassert_true(n <= MIN(len, used));
			for (uint32_t k = 0; k < n; k++) {
				zassert_equal(p[k], stream_byte(rd1 + k));
			}
			zassert_ok(ring_buf_dp_get_finish(&r, n));
			rd1 += n;
			break;
		case 4:
			len = MIN(len, sizeof(tmp));
			n = ring_buf_dp_peek(&r, tmp, len);
			zassert_equal(n, MIN(len, used2));
			for (uint32_t k = 0; k < n; k++) {
				zassert_equal(tmp[k], stream_byte(rd2 + k));
			}
			rd2 += n;
			break;
		case 5:
			n = ring_buf_dp_peek_claim(&r, &p, len);
			zassert_true(n <= MIN(len, used2));
			for (uint32_t k = 0; k < n; k++) {
				zassert_equal(p[k], stream_byte(rd2 + k));
			}
			zassert_ok(ring_buf_dp_peek_finish(&r, n));
			rd2 += n;
			break;
		case 6:
			if (!second) {
				zassert_ok(ring_buf_dp_second_enable(&r));
				second = true;
				rd2 = rd1;
			}
			break;
		default:
			if (second && (seed & 0x10000000U)) {
				ring_buf_dp_second_disable(&r);
				second = false;
			}
			break;
		}

		zassert_equal(ring_buf_dp_size_used(&r), wr - rd1);
		zassert_equal(ring_buf_dp_second_size_used(&r), second ? wr - rd2 : 0U);
	}
}

#define XFER_BYTES 3000
#define PROD_STACK 1024

static uint8_t xfer_mem[61];
static struct ring_buf_dp xfer_rb;
static K_THREAD_STACK_DEFINE(prod_stack, PROD_STACK);
static struct k_thread prod_thread;

static void producer(void *a, void *b, void *c)
{
	uint32_t sent = 0U;
	uint8_t *p;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (sent < XFER_BYTES) {
		uint32_t n = ring_buf_dp_put_claim(&xfer_rb, &p, MIN(13U, XFER_BYTES - sent));

		if (n == 0U) {
			k_msleep(1);
			continue;
		}

		for (uint32_t k = 0; k < n; k++) {
			p[k] = stream_byte(sent + k);
		}
		ring_buf_dp_put_finish(&xfer_rb, n);
		sent += n;
	}
}

ZTEST(ring_buf_dp_stress, test_producer_consumer_threads)
{
	uint32_t got = 0U;
	uint8_t *p;

	zassert_ok(ring_buf_dp_init(&xfer_rb, xfer_mem, sizeof(xfer_mem)));
	k_thread_create(&prod_thread, prod_stack, K_THREAD_STACK_SIZEOF(prod_stack), producer,
			NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	int64_t deadline = k_uptime_get() + 20000;

	while (got < XFER_BYTES && k_uptime_get() < deadline) {
		uint32_t n = ring_buf_dp_get_claim(&xfer_rb, &p, 17);

		if (n == 0U) {
			k_msleep(1);
			continue;
		}

		for (uint32_t k = 0; k < n; k++) {
			zassert_equal(p[k], stream_byte(got + k), "mismatch at %u", got + k);
		}
		zassert_ok(ring_buf_dp_get_finish(&xfer_rb, n));
		got += n;
	}

	zassert_equal(got, XFER_BYTES);
	zassert_ok(k_thread_join(&prod_thread, K_SECONDS(5)));
}

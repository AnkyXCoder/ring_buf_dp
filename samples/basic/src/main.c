/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 *
 * A producer writes in place with put_claim()/put_finish(), a consumer reads in
 * place with get_claim()/get_finish() and a monitor follows the same stream
 * through the secondary reader (peek_claim()/peek_finish()). All three verify
 * the byte pattern. The rbdp shell commands stay available afterwards.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ring_buf_dp.h>

#define BUF_SIZE    64
#define TOTAL_BYTES 1024
#define CHUNK_MAX   24
#define STACK_SIZE  1536

RING_BUF_DP_DECLARE(stream, BUF_SIZE);

static K_SEM_DEFINE(done_sem, 0, 3);
static atomic_t errors;

static inline uint8_t pattern(uint32_t pos)
{
	return (uint8_t)(pos * 5U + 1U);
}

static void producer(void *a, void *b, void *c)
{
	uint32_t sent = 0U;
	uint8_t *p;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (sent < TOTAL_BYTES) {
		uint32_t n = ring_buf_dp_put_claim(&stream, &p, MIN(CHUNK_MAX, TOTAL_BYTES - sent));

		if (n == 0U) {
			k_msleep(1);
			continue;
		}

		for (uint32_t i = 0; i < n; i++) {
			p[i] = pattern(sent + i);
		}
		ring_buf_dp_put_finish(&stream, n);
		sent += n;
	}

	printk("producer: sent %u bytes\n", sent);
	k_sem_give(&done_sem);
}

static void consumer(void *a, void *b, void *c)
{
	uint32_t got = 0U;
	uint8_t *p;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (got < TOTAL_BYTES) {
		uint32_t n = ring_buf_dp_get_claim(&stream, &p, CHUNK_MAX);

		if (n == 0U) {
			k_msleep(1);
			continue;
		}

		for (uint32_t i = 0; i < n; i++) {
			if (p[i] != pattern(got + i)) {
				atomic_inc(&errors);
			}
		}
		ring_buf_dp_get_finish(&stream, n);
		got += n;
	}

	printk("consumer: received %u bytes\n", got);
	k_sem_give(&done_sem);
}

static void monitor(void *a, void *b, void *c)
{
	uint32_t seen = 0U;
	uint8_t *p;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (seen < TOTAL_BYTES) {
		uint32_t n = ring_buf_dp_peek_claim(&stream, &p, CHUNK_MAX);

		if (n == 0U) {
			k_msleep(1);
			continue;
		}

		for (uint32_t i = 0; i < n; i++) {
			if (p[i] != pattern(seen + i)) {
				atomic_inc(&errors);
			}
		}
		ring_buf_dp_peek_finish(&stream, n);
		seen += n;
	}

	printk("monitor: observed %u bytes\n", seen);
	k_sem_give(&done_sem);
}

K_THREAD_DEFINE(producer_tid, STACK_SIZE, producer, NULL, NULL, NULL, 7, 0, -1);
K_THREAD_DEFINE(consumer_tid, STACK_SIZE, consumer, NULL, NULL, NULL, 7, 0, -1);
K_THREAD_DEFINE(monitor_tid, STACK_SIZE, monitor, NULL, NULL, NULL, 7, 0, -1);

int main(void)
{
	printk("ring_buf_dp sample: %d byte buffer, %d byte stream\n", BUF_SIZE, TOTAL_BYTES);

	ring_buf_dp_second_enable(&stream);
	k_thread_start(monitor_tid);
	k_thread_start(consumer_tid);
	k_thread_start(producer_tid);

	for (int i = 0; i < 3; i++) {
		k_sem_take(&done_sem, K_FOREVER);
	}

	printk("pattern errors: %ld\n", (long)atomic_get(&errors));
	printk(atomic_get(&errors) == 0 ? "SAMPLE PASSED\n" : "SAMPLE FAILED\n");
	printk("try: rbdp info, rbdp put hello, rbdp claim_get 4\n");
	return 0;
}

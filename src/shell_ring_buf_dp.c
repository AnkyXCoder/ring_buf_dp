/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <ring_buf_dp.h>

RING_BUF_DP_DECLARE(shell_rb, CONFIG_RING_BUF_DP_SHELL_BUF_SIZE);

static uint8_t *put_ptr;
static uint8_t *get_ptr;
static uint8_t *peek_ptr;
static uint8_t fill_counter;

static int parse_len(const struct shell *sh, const char *arg, uint32_t *len)
{
	char *end;
	unsigned long v = strtoul(arg, &end, 0);

	if (*arg == '\0' || *end != '\0' || v > CONFIG_RING_BUF_DP_SHELL_BUF_SIZE) {
		shell_error(sh, "invalid length '%s' (0..%d)", arg,
			    CONFIG_RING_BUF_DP_SHELL_BUF_SIZE);
		return -EINVAL;
	}

	*len = (uint32_t)v;
	return 0;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "size    : %u", ring_buf_dp_size_get(&shell_rb));
	shell_print(sh, "used    : %u", ring_buf_dp_size_used(&shell_rb));
	shell_print(sh, "second  : %u (%s)", ring_buf_dp_second_size_used(&shell_rb),
		    shell_rb.second_enabled ? "on" : "off");
	shell_print(sh, "space   : %u", ring_buf_dp_space_get(&shell_rb));
	shell_print(sh, "empty   : %s", ring_buf_dp_is_empty(&shell_rb) ? "yes" : "no");
	shell_print(sh, "full    : %s", ring_buf_dp_is_full(&shell_rb) ? "yes" : "no");
	return 0;
}

static int cmd_put(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t n = ring_buf_dp_put(&shell_rb, (const uint8_t *)argv[1], strlen(argv[1]));

	shell_print(sh, "put %u of %u bytes", n, (unsigned int)strlen(argv[1]));
	return 0;
}

static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t tmp[CONFIG_RING_BUF_DP_SHELL_BUF_SIZE];
	uint32_t len;
	uint32_t n;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	n = ring_buf_dp_get(&shell_rb, tmp, len);
	shell_print(sh, "got %u bytes", n);
	if (n != 0U) {
		shell_hexdump(sh, tmp, n);
	}
	return 0;
}

static int cmd_peek(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t tmp[CONFIG_RING_BUF_DP_SHELL_BUF_SIZE];
	uint32_t len;
	uint32_t n;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	n = ring_buf_dp_peek(&shell_rb, tmp, len);
	shell_print(sh, "peeked %u bytes", n);
	if (n != 0U) {
		shell_hexdump(sh, tmp, n);
	}
	return 0;
}

static int cmd_second(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "on") == 0) {
		int ret = ring_buf_dp_second_enable(&shell_rb);

		shell_print(sh, "secondary reader %s", ret == 0 ? "enabled" : "already enabled");
	} else if (strcmp(argv[1], "off") == 0) {
		ring_buf_dp_second_disable(&shell_rb);
		shell_print(sh, "secondary reader disabled");
	} else {
		shell_error(sh, "usage: rbdp second <on|off>");
		return -EINVAL;
	}
	return 0;
}

static int cmd_claim_put(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	uint32_t n;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	n = ring_buf_dp_put_claim(&shell_rb, &put_ptr, len);
	for (uint32_t i = 0; i < n; i++) {
		put_ptr[i] = fill_counter++;
	}
	shell_print(sh, "claimed %u bytes, filled with an incrementing pattern", n);
	return 0;
}

static int cmd_finish_put(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	int ret;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	ret = ring_buf_dp_put_finish(&shell_rb, len);
	if (ret != 0) {
		shell_error(sh, "finish failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "committed %u bytes, used=%u", len, ring_buf_dp_size_used(&shell_rb));
	return 0;
}

static int cmd_claim_get(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	uint32_t n;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	n = ring_buf_dp_get_claim(&shell_rb, &get_ptr, len);
	shell_print(sh, "claimed %u bytes", n);
	if (n != 0U) {
		shell_hexdump(sh, get_ptr, n);
	}
	return 0;
}

static int cmd_finish_get(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	int ret;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	ret = ring_buf_dp_get_finish(&shell_rb, len);
	if (ret != 0) {
		shell_error(sh, "finish failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "consumed %u bytes, used=%u", len, ring_buf_dp_size_used(&shell_rb));
	return 0;
}

static int cmd_peek_claim(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	uint32_t n;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	n = ring_buf_dp_peek_claim(&shell_rb, &peek_ptr, len);
	shell_print(sh, "claimed %u bytes", n);
	if (n != 0U) {
		shell_hexdump(sh, peek_ptr, n);
	}
	return 0;
}

static int cmd_peek_finish(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t len;
	int ret;

	if (parse_len(sh, argv[1], &len) != 0) {
		return -EINVAL;
	}

	ret = ring_buf_dp_peek_finish(&shell_rb, len);
	if (ret != 0) {
		shell_error(sh, "finish failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "consumed %u bytes, second=%u", len,
		    ring_buf_dp_second_size_used(&shell_rb));
	return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ring_buf_dp_reset(&shell_rb);
	shell_print(sh, "reset");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_rbdp,
	SHELL_CMD_ARG(info, NULL, "Show buffer state", cmd_info, 1, 0),
	SHELL_CMD_ARG(put, NULL, "<string> Copy a string into the buffer", cmd_put, 2, 0),
	SHELL_CMD_ARG(get, NULL, "<n> Copy and consume n bytes (primary reader)", cmd_get, 2, 0),
	SHELL_CMD_ARG(peek, NULL, "<n> Copy and consume n bytes (secondary reader)", cmd_peek, 2,
		      0),
	SHELL_CMD_ARG(second, NULL, "<on|off> Enable or disable the secondary reader",
		      cmd_second, 2, 0),
	SHELL_CMD_ARG(claim_put, NULL, "<n> Claim n writable bytes and fill them", cmd_claim_put,
		      2, 0),
	SHELL_CMD_ARG(finish_put, NULL, "<n> Commit n claimed bytes", cmd_finish_put, 2, 0),
	SHELL_CMD_ARG(claim_get, NULL, "<n> Claim n readable bytes and dump them", cmd_claim_get,
		      2, 0),
	SHELL_CMD_ARG(finish_get, NULL, "<n> Consume n claimed bytes", cmd_finish_get, 2, 0),
	SHELL_CMD_ARG(peek_claim, NULL, "<n> Claim n bytes for the secondary reader",
		      cmd_peek_claim, 2, 0),
	SHELL_CMD_ARG(peek_finish, NULL, "<n> Consume n bytes claimed by the secondary reader",
		      cmd_peek_finish, 2, 0),
	SHELL_CMD_ARG(reset, NULL, "Drop all data", cmd_reset, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(rbdp, &sub_rbdp, "Double pointer ring buffer commands", NULL);

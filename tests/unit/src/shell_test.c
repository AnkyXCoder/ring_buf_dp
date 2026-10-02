/*
 * Copyright (c) 2026 Ankit Modi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

static const struct shell *sh;

static const char *run(const char *cmd, int expected_ret)
{
	size_t size;

	shell_backend_dummy_clear_output(sh);
	zassert_equal(shell_execute_cmd(sh, cmd), expected_ret, "'%s'", cmd);
	return shell_backend_dummy_get_output(sh, &size);
}

static void *setup(void)
{
	sh = shell_backend_dummy_get_ptr();
	WAIT_FOR(shell_ready(sh), 20000, k_msleep(1));
	zassert_true(shell_ready(sh));
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	run("rbdp second off", 0);
	run("rbdp reset", 0);
}

ZTEST_SUITE(ring_buf_dp_shell, NULL, setup, before, NULL, NULL);

ZTEST(ring_buf_dp_shell, test_info)
{
	const char *out = run("rbdp info", 0);

	zassert_not_null(strstr(out, "size    : "));
	zassert_not_null(strstr(out, "used    : 0"));
	zassert_not_null(strstr(out, "empty   : yes"));
}

ZTEST(ring_buf_dp_shell, test_put_get)
{
	const char *out = run("rbdp put abc", 0);

	zassert_not_null(strstr(out, "put 3 of 3 bytes"), "%s", out);
	out = run("rbdp get 2", 0);
	zassert_not_null(strstr(out, "got 2 bytes"), "%s", out);
	zassert_not_null(strstr(out, "|ab"), "%s", out);
	out = run("rbdp info", 0);
	zassert_not_null(strstr(out, "used    : 1"), "%s", out);
}

ZTEST(ring_buf_dp_shell, test_claim_finish)
{
	const char *out = run("rbdp claim_put 4", 0);

	zassert_not_null(strstr(out, "claimed 4 bytes"), "%s", out);
	out = run("rbdp finish_put 4", 0);
	zassert_not_null(strstr(out, "committed 4 bytes, used=4"), "%s", out);
	out = run("rbdp claim_get 4", 0);
	zassert_not_null(strstr(out, "claimed 4 bytes"), "%s", out);
	out = run("rbdp finish_get 4", 0);
	zassert_not_null(strstr(out, "consumed 4 bytes, used=0"), "%s", out);
}

ZTEST(ring_buf_dp_shell, test_finish_too_much_fails)
{
	run("rbdp claim_put 2", 0);
	run("rbdp finish_put 9", -EINVAL);
}

ZTEST(ring_buf_dp_shell, test_second_reader)
{
	const char *out = run("rbdp second on", 0);

	zassert_not_null(strstr(out, "enabled"), "%s", out);
	run("rbdp put xyz", 0);
	out = run("rbdp peek 3", 0);
	zassert_not_null(strstr(out, "peeked 3 bytes"), "%s", out);
	zassert_not_null(strstr(out, "|xyz"), "%s", out);
	out = run("rbdp info", 0);
	zassert_not_null(strstr(out, "used    : 3"), "%s", out);
	run("rbdp second maybe", -EINVAL);
}

ZTEST(ring_buf_dp_shell, test_invalid_length)
{
	run("rbdp get abc", -EINVAL);
	run("rbdp get 100000", -EINVAL);
}

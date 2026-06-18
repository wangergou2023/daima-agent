#pragma once

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int _test_total;
static int _test_passed;
static int _test_failed;
static clock_t _test_start;

static void test_begin(void)
{
	_test_total = 0;
	_test_passed = 0;
	_test_failed = 0;
	_test_start = clock();
}

static void test_summary(void)
{
	double elapsed = (double)(clock() - _test_start) / CLOCKS_PER_SEC;
	printf("\n=== %d/%d passed, %d failed, %.2fs ===\n",
	       _test_passed, _test_total, _test_failed, elapsed);
}

#define TEST_ASSERT(cond, msg) do { \
	_test_total++; \
	if (!(cond)) { \
		printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
		_test_failed++; \
	} else { \
		_test_passed++; \
	} \
} while (0)

#define TEST_CASE(name) \
	printf("--- %s ---\n", name); \
	_test_total = _test_passed = _test_failed = 0; \
	_test_start = clock()

#define TEST_DONE() \
	printf("  ok (%d/%d)\n", _test_passed, _test_total)

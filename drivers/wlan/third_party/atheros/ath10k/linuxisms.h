#pragma once

#include <zircon/assert.h>

#include <stdio.h>

#define ARRAY_SIZE(arr) \
	(sizeof(arr) / sizeof(arr[0]))

#define BIT(pos) (1UL << (pos))

#define ETHTOOL_FWVERS_LEN 32

#define GENMASK1(val) ((1UL << (val)) - 1)
#define GENMASK(start, end) ((GENMASK1((start) + 1) & ~GENMASK1(end)))

#define SPINLOCK_ASSERT_HELD(lock) 								\
	do {											\
		int res = pthread_spin_trylock(lock);						\
		ZX_DEBUG_ASSERT(res != 0);							\
		if (res == 0) {									\
			printf("ath10k: spinlock not held at %s:%d\n", __FILE__, __LINE__);	\
			pthread_spin_unlock(lock);						\
		}										\
	} while (0)

#define WARN(cond, filename, lineno) \
	printf("ath10k: unexpected condition %s at %s:%d\n", cond, filename, lineno)

#define WARN_ON(cond) 						\
	({							\
		if (cond) {					\
			WARN(#cond, __FILE__, __LINE__);	\
		}						\
		cond;						\
	})

#define WARN_ON_ONCE(cond)					\
	({							\
		static bool warn_next = true;			\
		if (cond && warn_next) {			\
			WARN(#cond, __FILE__, __LINE__);	\
			warn_next = false;			\
		}						\
		cond;						\
	})

#define iowrite32(value, addr) 							\
	do {									\
		(*(volatile uint32_t*)(uintptr_t)(addr)) = (value);		\
	} while (0)

#define ioread32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))

#define mdelay(msecs) 										\
	do {											\
        	zx_time_t busy_loop_end = zx_time_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(msecs);	\
		while (zx_time_get(ZX_CLOCK_MONOTONIC) < busy_loop_end) {			\
		}										\
	} while (0)

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define min_t(t,a,b) (((t)(a) < (t)(b)) ? (t)(a) : (t)(b))

#define __packed __attribute__((packed))

#define rounddown(n,m) ((n) - ((n) % (m)))
#define roundup(n,m) (((n) % (m) == 0) ? (n) : (n) + ((m) - ((n) % (m))))

#define roundup_pow_of_two(val) \
	((unsigned long) (val) == 0 ? (val) : \
			 1UL << ((sizeof(unsigned long) * 8) - __builtin_clzl((val) - 1)))

/* Not actually a linuxism, but closely related to the previous definition */
#define roundup_log2(val) \
	((unsigned long) (val) == 0 ? (val) : \
			 ((sizeof(unsigned long) * 8) - __builtin_clzl((val) - 1)))


#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "minimap.h"
#include "mmpriv.h"
#include "kalloc.h"

static const char LogTable256[256] = {
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    LT(4), LT(5), LT(5), LT(6), LT(6), LT(6), LT(6),
    LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)
};

static inline int ilog2_32(uint32_t v)
{
	register uint32_t t, tt;
	if ((tt = v>>16)) return (t = tt>>8) ? 24 + LogTable256[t] : 16 + LogTable256[tt];
	return (t = v>>8) ? 8 + LogTable256[t] : LogTable256[v];
}

int mm_chain_dp(int match_len, int max_dist, int bw, int max_skip, int min_sc, int n, mm128_t *a, uint64_t **_u, void *km)
{
	int32_t st = 0, i, j, k, *p, *f, *t, *v, n_u, n_v;
	uint64_t *u;
	mm128_t *b;

	if (_u) *_u = 0;
	f = (int32_t*)kmalloc(km, n * 4);
	p = (int32_t*)kmalloc(km, n * 4);
	t = (int32_t*)kmalloc(km, n * 4);
	memset(t, 0, n * 4);

	// fill the score and backtrack arrays
	for (i = 0; i < n; ++i) {
		uint64_t ri = a[i].x;
		int32_t qi = (int32_t)a[i].y;
		int32_t max_f = -INT32_MAX, max_j = -1, n_skip = 0;
		while (st < i && ri - a[st].x > max_dist) ++st;
		for (j = i - 1; j >= st; --j) {
			int64_t dr = ri - a[j].x;
			int32_t dq = qi - (int32_t)a[j].y, dd, sc;
			if (dq <= 0 || dq > max_dist) continue;
			if (t[j] == i) {
				if (p[j] >= 0) t[p[j]] = i;
				if (++n_skip > max_skip) break;
				continue;
			}
			dd = dr > dq? dr - dq : dq - dr;
			if (dd > bw) continue;
			sc = dq > match_len && dr > match_len? match_len : dq < dr? dq : dr;
			sc = f[j] + sc - ilog2_32(dd);
			if (sc > max_f) max_f = sc, max_j = j;
		}
		if (max_j >= 0) f[i] = max_f, p[i] = max_j;
		else f[i] = match_len, p[i] = -1;
	}

	// find the ending positions of chains
	memset(t, 0, n * 4);
	for (i = 0; i < n; ++i)
		if (p[i] >= 0) t[p[i]] = 1;
	for (i = n_u = 0; i < n; ++i)
		if (t[i] == 0 && f[i] >= min_sc)
			++n_u;
	if (n_u == 0) return 0;
	u = (uint64_t*)kmalloc(km, n_u * 8);
	for (i = n_u = 0; i < n; ++i)
		if (t[i] == 0 && f[i] >= min_sc)
			u[n_u++] = (uint64_t)f[i] << 32 | i;
	radix_sort_64(u, u + n_u);

	// backtrack
	memset(t, 0, n * 4);
	v = (int32_t*)kmalloc(km, n * 4);
	for (i = n_u - 1, n_v = k = 0; i >= 0; --i) { // starting from the highest score
		int32_t n_v0 = n_v;
		j = (int32_t)u[i];
		do {
			v[n_v++] = j;
			t[j] = 1;
			j = p[j];
		} while (j >= 0 && t[j] == 0);
		if (j < 0) u[k++] = u[i]>>32<<32 | (n_v - n_v0);
		else if ((u[i]>>32) - f[j] >= min_sc) u[k++] = ((u[i]>>32) - f[j]) << 32 | (n_v - n_v0);
		else n_v0 = n_v;
		for (j = 0; j < (n_v - n_v0) >> 1; ++j) { // reverse v[] such that the smallest index comes the first
			int32_t t = v[n_v0 + j];
			v[n_v0 + j] = v[n_v - 1 - j];
			v[n_v - 1 - j] = t;
		}
	}
	n_u = k, *_u = u;

	// free
	kfree(km, f); kfree(km, p); kfree(km, t);

	// write the result to _a_
	b = kmalloc(km, n_v * sizeof(mm128_t));
	for (i = 0, k = 0; i < n_u; ++i)
		for (j = 0; j < (int32_t)u[i]; ++j)
			b[k] = a[v[k]], ++k;
	memcpy(a, b, n_v * sizeof(mm128_t));
	kfree(km, b);
	return n_u;
}

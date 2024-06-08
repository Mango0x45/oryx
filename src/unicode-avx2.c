/*
 * MIT License
 *
 * Copyright © 2019 Yibo Cai
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Source: https://github.com/cyb70289/utf8
 */

#include <stdint.h>
#include <x86intrin.h>

#include "unicode.h"

#pragma GCC diagnostic ignored "-Woverflow"

static const int8_t _first_len_tbl[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3,
};

static const int8_t _first_range_tbl[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8,
};

static const int8_t _range_min_tbl[] = {
	0x00, 0x80, 0x80, 0x80, 0xA0, 0x80, 0x90, 0x80, 0xC2, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x80, 0x80, 0x80, 0xA0, 0x80,
	0x90, 0x80, 0xC2, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};
static const int8_t _range_max_tbl[] = {
	0x7F, 0xBF, 0xBF, 0xBF, 0xBF, 0x9F, 0xBF, 0x8F, 0xF4, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0xBF, 0xBF, 0xBF, 0xBF, 0x9F,
	0xBF, 0x8F, 0xF4, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

static const int8_t _df_ee_tbl[] = {
	0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,
	0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,
};

static const int8_t _ef_fe_tbl[] = {
	0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static inline __m256i
push_last_byte_of_a_to_b(__m256i a, __m256i b)
{
	return _mm256_alignr_epi8(b, _mm256_permute2x128_si256(a, b, 0x21), 15);
}

static inline __m256i
push_last_2bytes_of_a_to_b(__m256i a, __m256i b)
{
	return _mm256_alignr_epi8(b, _mm256_permute2x128_si256(a, b, 0x21), 14);
}

static inline __m256i
push_last_3bytes_of_a_to_b(__m256i a, __m256i b)
{
	return _mm256_alignr_epi8(b, _mm256_permute2x128_si256(a, b, 0x21), 13);
}

bool
utf8_validate_simd(const unsigned char *s, size_t len)
{
	if (len >= 32) {
		__m256i prev_input = _mm256_set1_epi8(0);
		__m256i prev_first_len = _mm256_set1_epi8(0);

		const __m256i first_len_tbl = _mm256_loadu_si256(
			(const __m256i *)_first_len_tbl);
		const __m256i first_range_tbl = _mm256_loadu_si256(
			(const __m256i *)_first_range_tbl);
		const __m256i range_min_tbl = _mm256_loadu_si256(
			(const __m256i *)_range_min_tbl);
		const __m256i range_max_tbl = _mm256_loadu_si256(
			(const __m256i *)_range_max_tbl);
		const __m256i df_ee_tbl = _mm256_loadu_si256(
			(const __m256i *)_df_ee_tbl);
		const __m256i ef_fe_tbl = _mm256_loadu_si256(
			(const __m256i *)_ef_fe_tbl);

		__m256i error1 = _mm256_set1_epi8(0);
		__m256i error2 = _mm256_set1_epi8(0);

		while (len >= 32) {
			const __m256i input = _mm256_loadu_si256((const __m256i *)s);

			const __m256i high_nibbles = _mm256_and_si256(
				_mm256_srli_epi16(input, 4), _mm256_set1_epi8(0x0F));

			__m256i first_len = _mm256_shuffle_epi8(first_len_tbl,
			                                        high_nibbles);

			__m256i range = _mm256_shuffle_epi8(first_range_tbl, high_nibbles);

			range = _mm256_or_si256(
				range, push_last_byte_of_a_to_b(prev_first_len, first_len));

			__m256i tmp1, tmp2;

			tmp1 = push_last_2bytes_of_a_to_b(prev_first_len, first_len);
			tmp2 = _mm256_subs_epu8(tmp1, _mm256_set1_epi8(1));

			range = _mm256_or_si256(range, tmp2);

			tmp1 = push_last_3bytes_of_a_to_b(prev_first_len, first_len);
			tmp2 = _mm256_subs_epu8(tmp1, _mm256_set1_epi8(2));
			range = _mm256_or_si256(range, tmp2);

			__m256i shift1, pos, range2;

			shift1 = push_last_byte_of_a_to_b(prev_input, input);
			pos = _mm256_sub_epi8(shift1, _mm256_set1_epi8(0xEF));

			tmp1 = _mm256_subs_epu8(pos, _mm256_set1_epi8(240));
			range2 = _mm256_shuffle_epi8(df_ee_tbl, tmp1);
			tmp2 = _mm256_adds_epu8(pos, _mm256_set1_epi8(112));
			range2 = _mm256_add_epi8(range2,
			                         _mm256_shuffle_epi8(ef_fe_tbl, tmp2));

			range = _mm256_add_epi8(range, range2);

			__m256i minv = _mm256_shuffle_epi8(range_min_tbl, range);
			__m256i maxv = _mm256_shuffle_epi8(range_max_tbl, range);

			error1 = _mm256_or_si256(error1, _mm256_cmpgt_epi8(minv, input));
			error2 = _mm256_or_si256(error2, _mm256_cmpgt_epi8(input, maxv));

			prev_input = input;
			prev_first_len = first_len;

			s += 32;
			len -= 32;
		}

		__m256i error = _mm256_or_si256(error1, error2);
		if (!_mm256_testz_si256(error, error))
			return false;

		int32_t token4 = _mm256_extract_epi32(prev_input, 7);
		const int8_t *token = (const int8_t *)&token4;
		int lookahead = 0;
		if (token[3] > (int8_t)0xBF)
			lookahead = 1;
		else if (token[2] > (int8_t)0xBF)
			lookahead = 2;
		else if (token[1] > (int8_t)0xBF)
			lookahead = 3;

		s -= lookahead;
		len += lookahead;
	}

	/* Check remaining bytes with naïve method */
	return utf8_validate_off(s, len) == 0;
}

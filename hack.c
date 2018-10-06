#include <smmintrin.h>
#include <inttypes.h>

// https://software.intel.com/sites/landingpage/IntrinsicsGuide/#techs=SSE,SSE2,SSE3,SSSE3,SSE4_1,SSE4_2&expand=5644,5587,5105,4751,1015,1015,5481,4702,4620,4673,4673,5723,3611,432&cats=Arithmetic
// https://woboq.Com/blog/utf-8-processing-using-simd.html

// From gdb include/leb128.h -- not the one gdb actually uses but very
// similar.
size_t
gdb_read_uleb128_to_uint64 (const unsigned char *buf,
			    const unsigned char *buf_end,
			    uint64_t *r)
{
  const unsigned char *p = buf;
  unsigned int shift = 0;
  uint64_t result = 0;
  unsigned char byte;

  while (1)
    {
      if (p >= buf_end)
	return 0;

      byte = *p++;
      result |= ((uint64_t) (byte & 0x7f)) << shift;
      if ((byte & 0x80) == 0)
	break;
      shift += 7;
    }

  *r = result;
  return p - buf;
}

size_t
unrolled_read_uleb128_to_uint64 (const unsigned char *buf,
				 const unsigned char *buf_end,
				 uint64_t *r)
{
  const unsigned char *p = buf;
  uint64_t result = 0;
  unsigned char byte;

#define unlikely(x) __builtin_expect ((x), 0)

#define STEP(Num)						\
  if (unlikely (p >= buf_end))					\
    return 0;							\
  byte = *p++;							\
  result |= ((uint64_t) (byte & 0x7f)) << (7 * Num);		\
  /* FIXME check to see which of these should be likely or	\
     unlikely.  */						\
  if ((byte & 0x80) == 0)					\
    {								\
      *r = result;						\
      return Num + 1;						\
    }

  STEP (0);
  STEP (1);
  STEP (2);
  STEP (3);
  STEP (4);
  STEP (5);
  STEP (6);
  STEP (7);
  STEP (8);
  STEP (9);

  return 0;
}

// FIXME really just doing 2 bytes up front seems better.
// Has same API as the above.
size_t
read_uleb128 (const unsigned char *bytes, const unsigned char *buf_end,
	      uint64_t *result)
{
  // FIXME if there are < 16 bytes in the buffer, call
  // gdb_read_uleb128_to_uint64.

  // FIXME could use __mm_lddqu_si128 in SSE3.
  __m128i raw = _mm_loadu_si128 ((__m128i *) bytes);

  // First invert the continuation bits.  Now a zero means "keep the
  // next byte".  This also makes the values more usable -- but note
  // that the high bit of the high byte will remain set.  We remove it
  // at the end.
  __m128i k = _mm_set1_epi8 (0x80);
  raw = _mm_xor_si128 (raw, k);

  // The continuation bit affects the following byte, so shift left by
  // one.  This shifts in a zero, which means "keep" because we
  // inverted.
  __m128i mask = _mm_bslli_si128 (raw, 1);

  // The continuation bit affects the following byte.  Since we
  // inverted the continuation bits, we want to find the first byte
  // with a non-zero high bit, and then ignore all subsequent bytes.
  // It might be possible to do this by propagating high bits via a
  // series of "or"s, but since we also need to know how many bytes
  // we're processing, we instead count the trailing zeros.
  int bit_mask = _mm_movemask_epi8 (mask);
  // We always want to drop the 6 right-most bytes, because we only
  // support up to 10-byte sequences of uleb128.  This also lets us
  // use clz, because now the mask is known not to be 0.
  bit_mask |= 1 << 10;
  
  // Number of bytes we intend to process.
  int nbytes = __builtin_ctz (bit_mask);

  // Drop the rightmost bytes by updating the mask.
  // FIXME this can't be good.
  __m128i indices = _mm_setr_epi8 (0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
				   10, 11, 12, 13, 14, 15);
  __m128i compare = _mm_set1_epi8 (nbytes);

  // After this, each byte in MASK is either 0xff, to keep the value,
  // or 0, to drop it.
  mask = _mm_cmpgt_epi8 (compare, indices);

  // Clear the bytes we don't need.
  __m128i value = _mm_and_si128 (mask, raw);

  // Now mask off the one lingering high bit.
  mask  = _mm_set1_epi8 (0x7f);
  value = _mm_and_si128 (mask, value);

  // Now we have a byte vector like:
  //   0123456789
  // Each byte holds 7 bits of payload.  So, for example, we need to
  // shift byte 1 right one bit.  So, we split this into parts like:
  //   0.2.4.6.8
  //   .1.3.5.7.9
  // Then we shift and or, to get 16 bit values like:
  //   0011223344
  __m128i himask = _mm_set1_epi16 (0xff00);
  __m128i lo_v = _mm_andnot_si128 (himask, value);
  __m128i hi_v = _mm_and_si128 (himask, value);
  hi_v = _mm_srli_epi16 (hi_v, 1);
  value = _mm_or_si128 (lo_v, hi_v);

  // Our values are now 14 bits of payload in 16 bit slots, like:
  //   0011223344
  // Do the masking again:
  //   00..22..44
  //   ..11..33..
  // Shift the high word by 2 bits, then or them together.
  himask = _mm_set1_epi32 (0xffff0000);
  lo_v = _mm_andnot_si128 (himask, value);
  hi_v = _mm_and_si128 (himask, value);
  hi_v = _mm_srli_epi32 (hi_v, 2);
  value = _mm_or_si128 (lo_v, hi_v);

  // Now we have 28 bits of payload in 32 bit slots, like:
  //    0000111144
  // So one more masking operation, this time shifting by 4 bits.
  himask = _mm_set1_epi64x (0xffffffff00000000);
  lo_v = _mm_andnot_si128 (himask, value);
  hi_v = _mm_and_si128 (himask, value);
  hi_v = _mm_srli_epi64 (hi_v, 4);
  value = _mm_or_si128 (lo_v, hi_v);

  // Now we have 56 bits of payload in one 64 bit slot, and then an
  // additional 8 bits of payload in another 64 bit slot, like:
  //    000000001
  int64_t v1 = _mm_extract_epi64 (value, 0);
  int64_t v2 = _mm_extract_epi64 (value, 1);

#if 0
  uint64_t r = v1 | (v2 << 56);

  // Finally we have to strip off the high bit, which was left over
  // from the inversion stage, above.
  int highzeros = __builtin_clzll (r);
  *result = r & ~(1ull << (63 - highzeros));
#else
  *result = v1 | (v2 << 56);
#endif

  return nbytes;
}

size_t
skip_leb128 (const unsigned char *bytes)
{
  // FIXME could use __mm_lddqu_si128 in SSE3.
  __m128i raw = _mm_loadu_si128 ((__m128i *) bytes);

  // First invert the continuation bits.  Now a zero means "keep the
  // next byte".  This also makes the values more usable.
  __m128i k = _mm_set1_epi8 (0x80);
  raw = _mm_xor_si128 (raw, k);

  // The continuation bit affects the following byte, so shift right
  // by one.  This shifts in a zero, which means "keep" because we
  // inverted.
  __m128i mask = _mm_bsrli_si128 (raw, 1);

  // The continuation bit affects the following byte.  Since we
  // inverted the continuation bits, we want to find the first byte
  // with a non-zero high bit, and then ignore all subsequent bytes.
  // It might be possible to do this by propagating high bits via a
  // series of "or"s, but since we also need to know how many bytes
  // we're processing, we instead count the trailing zeros.
  int bit_mask = _mm_movemask_epi8 (mask);
  // We always want to drop the 6 right-most bytes, because we only
  // support up to 10-byte sequences of uleb128.  This also lets us
  // use clz, because now the mask is known not to be 0.
  bit_mask |= 1 << 10;
  
  // Number of bytes we intend to process.
  return __builtin_ctz (bit_mask);
}

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

typedef unsigned char bfd_byte;

extern size_t gdb_read_uleb128_to_uint64 (const unsigned char *buf,
					  const unsigned char *buf_end,
					  uint64_t *r);
extern size_t unrolled_read_uleb128_to_uint64 (const unsigned char *buf,
					       const unsigned char *buf_end,
					       uint64_t *r);
extern size_t read_uleb128 (const unsigned char *bytes,
			    const unsigned char *buf_end,
			    uint64_t *result);

/* Write VAL in uleb128 format to P, returning a pointer to the
   following byte.  */
static bfd_byte *
write_uleb128 (bfd_byte *p, uint64_t val)
{
  bfd_byte c;
  do
    {
      c = val & 0x7f;
      val >>= 7;
      if (val)
	c |= 0x80;
      *(p++) = c;
    }
  while (val);
  return p;
}

static const bfd_byte *
make_tests (const bfd_byte **end)
{
  // 2 M of tests.
  size_t amt = 2 * 1024 * 1024;
  bfd_byte *mem = malloc (amt);

  bfd_byte *ptr = mem;
  bfd_byte *prev = mem;
  while (ptr < mem + amt - 16)
    {
      prev = ptr;

      uint64_t value = random ();
      // Do half 32 bit values, since those are common (FIXME verify
      // this); and half larger value.
      if (ptr - mem > amt / 2)
	{
	  value <<= 32;
	  value |= (unsigned long) random ();
	}

      ptr = write_uleb128 (ptr, value);
    }

  *end = prev;
  return mem;
}

static void
check (const bfd_byte *bytes, const bfd_byte *end)
{
  while (bytes < end)
    {
      uint64_t val1, val2, val3;
      size_t len1 = gdb_read_uleb128_to_uint64 (bytes, end, &val1);
      size_t len2 = read_uleb128 (bytes, end, &val2);
      size_t len3 = unrolled_read_uleb128_to_uint64 (bytes, end, &val3);

      if (len1 != len2)
	printf ("oops on length: val=%lu: %lu -> %lu\n",
		(unsigned long) val1,
		(unsigned long) len1, (unsigned long) len2);
      else if (val1 != val2)
	printf ("oops on value: %lu -> %lu\n",
		(unsigned long) val1, (unsigned long) val2);
      else if (len1 != len3)
	printf ("oops on length3: val=%lu: %lu -> %lu\n",
		(unsigned long) val1,
		(unsigned long) len1, (unsigned long) len3);
      else if (val1 != val3)
	printf ("oops on value3: %lu -> %lu\n",
		(unsigned long) val1, (unsigned long) val3);
      else
	{
	  bytes += len1;
	  continue;
	}

      printf ("bytes: ");
      for (int i = 0; i < len1; ++i)
	printf ("%02x ", bytes[i]);
      printf ("\n");

      exit (1);
    }
}

#define FUNC(Name, Callee)				\
static void						\
Name (const bfd_byte *bytes, const bfd_byte *end)	\
{							\
  while (bytes < end)					\
    {							\
      uint64_t val;					\
      size_t len = Callee (bytes, end, &val);		\
      bytes += val;					\
    }							\
}

FUNC (t1, gdb_read_uleb128_to_uint64)
FUNC (t2, read_uleb128)
FUNC (t3, unrolled_read_uleb128_to_uint64)

int
main ()
{
  const bfd_byte *end;
  const bfd_byte *bytes = make_tests (&end);

  check (bytes, end);

#define COUNT 5000

#define TEST(Name, Func)						\
  {									\
    clock_t cstart = clock ();						\
    for (int i = 0; i < COUNT; ++i)					\
      Func (bytes, end);						\
    clock_t cend  = clock ();						\
									\
    double cpu_time_used = ((double) (cend - cstart)) / CLOCKS_PER_SEC;	\
    printf (Name " = %f\n", cpu_time_used);				\
  }

  TEST ("     gdb", t1);
  TEST ("     sse", t2);
  TEST ("unrolled", t3);

  return 0;
}

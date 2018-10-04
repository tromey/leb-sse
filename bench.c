#include <stdlib.h>
#include <inttypes.h>

typedef unsigned char bfd_byte;

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

static bfd_byte *
make_tests ()
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

  return prev;
}

static void
check (const gdb_byte *bytes)

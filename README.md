This is an attempt to decode uleb128 using SSE.

It performs about the same as the obvious C code.
Unrolling the loop by hand seems better so far.

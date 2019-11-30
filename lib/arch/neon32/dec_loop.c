static inline int
is_nonzero (const uint8x16_t v)
{
	uint64_t u64;
	const uint64x2_t v64 = vreinterpretq_u64_u8(v);
	const uint32x2_t v32 = vqmovn_u64(v64);

	vst1_u64(&u64, vreinterpret_u64_u32(v32));
	return u64 != 0;
}

static inline uint8x16x3_t
dec_reshuffle (const uint8x16x4_t in)
{
	uint8x16x3_t out;
#if 0
	// Allow the compiler to reuse registers:
	out.val[2] = in.val[3];

	__asm__ (
		"vshr.u8 %q[o0], %q[i1], #4    \n\t"
		"vshr.u8 %q[o1], %q[i2], #2    \n\t"
		"vsli.8  %q[o2], %q[i2], #6    \n\t"
		"vsli.8  %q[o0], %q[i0], #2    \n\t"
		"vsli.8  %q[o1], %q[i1], #4    \n\t"

		// Outputs:
		: [o0] "=&w" (out.val[0]),
		  [o1] "=&w" (out.val[1]),
		  [o2] "+w"  (out.val[2])

		// Inputs:
		: [i0] "w" (in.val[0]),
		  [i1] "w" (in.val[1]),
		  [i2] "w" (in.val[2])
	);
#else
	out.val[0] = vshrq_n_u8(in.val[1], 4);
	out.val[1] = vshrq_n_u8(in.val[2], 2);
	out.val[2] = in.val[3];

	out.val[0] = vsliq_n_u8(out.val[0], in.val[0], 2);
	out.val[1] = vsliq_n_u8(out.val[1], in.val[1], 4);
	out.val[2] = vsliq_n_u8(out.val[2], in.val[2], 6);
#endif
	return out;
}

static inline uint8x16_t
delta_lookup (const uint8x16_t v)
{
	const uint8x8_t lut = {
		0, 16, 19, 4, (uint8_t) -65, (uint8_t) -65, (uint8_t) -71, (uint8_t) -71,
	};

	return vcombine_u8(
		vtbl1_u8(lut, vget_low_u8(v)),
		vtbl1_u8(lut, vget_high_u8(v)));
}

static inline uint8x16_t
lut_hi_lookup (const uint8x16_t v)
{
	const uint8x8_t lut = {
		0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08,
	};

	// Out-of-range indices should return 0x10:
	const uint8x8_t upper = vdup_n_u8(0x10);

	return vcombine_u8(
		vtbx1_u8(upper, lut, vget_low_u8(v)),
		vtbx1_u8(upper, lut, vget_high_u8(v)));
}

static inline uint8x16_t
dec_loop_neon32_lane (uint8x16_t *lane)
{
	// See the SSSE3 decoder for an explanation of the algorithm.
	const uint8x16_t lut_lo = {
		0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
		0x11, 0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A
	};

#if 0
	const uint8x8_t lut_hi = {
		0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08,
	};

	uint8x16_t hi, eq_2F, lo_nibbles, hi_nibbles;

	__asm__(
		"vshr.u8 %q[hn], %q[in], #4      \n\t"
		"vshl.u8 %q[ln], %q[in], #4      \n\t"
		"vshr.u8 %q[ln], %q[ln], #4      \n\t"
		"vmov.u8 %q[ef], #0x2F           \n\t"
		"vceq.i8 %q[ef], %q[ef], %q[in]  \n\t"

		"vmov.u8 %q[hi], #0x10              \n\t"
		"vtbx.8  %e[hi], { %[lh] }, %e[hn]  \n\t"
		"vtbx.8  %f[hi], { %[lh] }, %f[hn]  \n\t"

		// Outputs:
		: [in] "+&w" (*lane),
		  [hi] "=&w" (hi),
		  [ln] "=&w" (lo_nibbles),
		  [hn] "=&w" (hi_nibbles),
		  [ef] "=&w" (eq_2F)

		// Inputs:
		: [lh] "w" (lut_hi)
	);

#else
	const uint8x16_t mask_2F = vdupq_n_u8(0x2F);

	const uint8x16_t hi_nibbles = vshrq_n_u8(*lane, 4);
	const uint8x16_t lo_nibbles = vshrq_n_u8(vshlq_n_u8(*lane, 4), 4);
	const uint8x16_t eq_2F      = vceqq_u8(*lane, mask_2F);
	const uint8x16_t hi         = lut_hi_lookup(hi_nibbles);
#endif

	const uint8x16_t lo = vqtbl1q_u8(lut_lo, lo_nibbles);

	// Now simply add the delta values to the input:
	*lane = vaddq_u8(*lane, delta_lookup(vaddq_u8(eq_2F, hi_nibbles)));

	// Return the validity mask:
	return vandq_u8(lo, hi);
}

static inline void
dec_loop_neon32 (const uint8_t **s, size_t *slen, uint8_t **o, size_t *olen)
{
	if (*slen < 64) {
		return;
	}

	// Process blocks of 64 bytes per round. Unlike the SSE codecs, no
	// extra trailing zero bytes are written, so it is not necessary to
	// reserve extra input bytes:
	size_t rounds = *slen / 64;

	*slen -= rounds * 64;	// 64 bytes consumed per round
	*olen += rounds * 48;	// 48 bytes produced per round

	do {
		// Load 64 bytes and deinterleave:
		uint8x16x4_t str = vld4q_u8(*s);

		// Decode each lane, collect a mask of invalid inputs:
		const uint8x16_t classified
			= dec_loop_neon32_lane(&str.val[0])
			| dec_loop_neon32_lane(&str.val[1])
			| dec_loop_neon32_lane(&str.val[2])
			| dec_loop_neon32_lane(&str.val[3]);

		// Check for invalid input: if any of the delta values are
		// zero, fall back on bytewise code to do error checking and
		// reporting:
		if (is_nonzero(classified)) {
			break;
		}

		// Compress four bytes into three:
		const uint8x16x3_t out = dec_reshuffle(str);

		// Interleave and store decoded result:
		vst3q_u8(*o, out);

		*s += 64;
		*o += 48;

	} while (--rounds > 0);

	// Adjust for any rounds that were skipped:
	*slen += rounds * 64;
	*olen -= rounds * 48;
}

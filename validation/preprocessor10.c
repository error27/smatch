/*
 * result should be
 * defined
 * since concatenation of 'defi' and 'ned' should result in the same token
 * we would get if we had 'defined' in the input stream.
 */
#define A
#define B defi ## ned
#if B(A)
defined
#else
undefined
#endif

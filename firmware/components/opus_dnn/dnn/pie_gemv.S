/*
 * int8 matrix-vector product kernels for the ESP32-S3's PIE (SIMD) unit.
 *
 * void pie_gemv_kN(const int8_t *w, const int8_t *x, int32_t *acc, int rows)
 *
 * Adds to acc[r] (r = 0..rows-1) the dot product of x[0..16N-1] with row r of
 * a block of weights stored row after row, 16N bytes each (so a whole layer is
 * stored as blocks of up to 96 inputs, each block holding every row's slice).
 * w and x must be 16-byte aligned, and w must have 16 bytes readable past the
 * end of the block.
 *
 * x stays in q0..q(N-1) for the whole block. Each 16-element step is one
 * ee.vmulas.s8.accx.ld.ip, which multiplies one 16-byte slice of a row and
 * loads the next slice (across row boundaries, so the next row's first slice
 * is already loaded when it starts), alternating between q6 and q7. N is even,
 * so every row starts with its first slice in q6.
 */

    .macro PIE_GEMV n
    .text
    .align  4
    .global pie_gemv_k\n
    .type   pie_gemv_k\n, @function
pie_gemv_k\n:
    entry   a1, 32
    // Load the input slice into q0..q(n-1).
    ee.vld.128.ip   q0, a3, 16
    .if \n >= 2
    ee.vld.128.ip   q1, a3, 16
    .endif
    .if \n >= 4
    ee.vld.128.ip   q2, a3, 16
    ee.vld.128.ip   q3, a3, 16
    .endif
    .if \n >= 6
    ee.vld.128.ip   q4, a3, 16
    ee.vld.128.ip   q5, a3, 16
    .endif
    movi.n  a6, 0
    ee.vld.128.ip   q6, a2, 16          // first slice of row 0
    loopgtz a5, .Lrows_end_\n
    ee.zero.accx
    ee.vmulas.s8.accx.ld.ip q7, a2, 16, q6, q0
    ee.vmulas.s8.accx.ld.ip q6, a2, 16, q7, q1
    .if \n >= 4
    ee.vmulas.s8.accx.ld.ip q7, a2, 16, q6, q2
    ee.vmulas.s8.accx.ld.ip q6, a2, 16, q7, q3
    .endif
    .if \n >= 6
    ee.vmulas.s8.accx.ld.ip q7, a2, 16, q6, q4
    ee.vmulas.s8.accx.ld.ip q6, a2, 16, q7, q5
    .endif
    l32i    a8, a4, 0
    ee.srs.accx a7, a6, 0
    add     a8, a8, a7
    s32i    a8, a4, 0
    addi    a4, a4, 4
.Lrows_end_\n:
    retw.n
    .size   pie_gemv_k\n, . - pie_gemv_k\n
    .endm

    PIE_GEMV 2
    PIE_GEMV 4
    PIE_GEMV 6

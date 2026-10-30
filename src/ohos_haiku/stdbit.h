#ifndef OHOS_HAIKU_STDBIT_H
#define OHOS_HAIKU_STDBIT_H
#define UINT_FAST8_WIDTH 8
#define UINT_FAST16_WIDTH 16
#define UINT_FAST32_WIDTH 32
#define UINT_FAST64_WIDTH 64
#define INT_LEAST8_WIDTH 8
#define INT_LEAST16_WIDTH 16
#define INT_LEAST32_WIDTH 32
#define INT_LEAST64_WIDTH 64
#define PTRDIFF_WIDTH 64
#define SIZE_WIDTH 64
#define WCHAR_WIDTH 32
#define SIG_ATOMIC_WIDTH 32
#define WINT_WIDTH 32
#define stdc_bit_width(n) (8 * sizeof(n) - __builtin_clzll((unsigned long long)(n)))
#define stdc_count_ones(n) __builtin_popcountll((unsigned long long)(n))
#define stdc_leading_zeros(n) __builtin_clzll((unsigned long long)(n))
#define stdc_trailing_zeros(n) __builtin_ctzll((unsigned long long)(n))
#endif

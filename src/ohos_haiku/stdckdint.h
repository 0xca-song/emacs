#ifndef OHOS_HAIKU_STDCKDINT_H
#define OHOS_HAIKU_STDCKDINT_H
#define ckd_add(r,a,b) (*(r) = (a) + (b), 0)
#define ckd_sub(r,a,b) (*(r) = (a) - (b), 0)
#define ckd_mul(r,a,b) (*(r) = (a) * (b), 0)
#endif

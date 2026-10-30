#ifndef OHOS_HAIKU_OS_H
#define OHOS_HAIKU_OS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
typedef int32_t int32; typedef uint32_t uint32;
typedef int64_t int64; typedef uint64_t uint64;
typedef int32_t status_t; typedef int32_t port_id; typedef int32_t team_id;
typedef int64_t bigtime_t;
typedef struct { uint32_t object; uint16_t type; uint16_t events; } object_wait_info;
#define B_OK 0
#define B_ERROR -1
#define B_TIMEOUT -2
#define B_BAD_VALUE -3
#define B_ENTRY_NOT_FOUND -4
#define B_PERMISSION_DENIED -5
#define B_LINK_LIMIT -6
#define B_BUSY -7
#define B_NO_MORE_FDS -8
#define B_FILE_ERROR -9
#define B_NO_MEMORY -10
#define B_ALREADY_RUNNING -11
#define B_EVENT_READ 1
#define B_INFINITE_TIMEOUT (-1LL)
#define B_ABSOLUTE_TIMEOUT 1
#define B_OBJECT_TYPE_PORT 1
#define B_PAGE_SIZE 4096
#ifdef __cplusplus
extern "C" {
#endif
status_t wait_for_objects(object_wait_info *, int);
status_t wait_for_objects_etc(object_wait_info *, int, uint32_t, bigtime_t);
status_t write_port(port_id, int32_t, const void *, size_t);
ssize_t read_port(port_id, int32_t *, void *, size_t);
ssize_t port_buffer_size(port_id);
ssize_t port_buffer_size_etc(port_id, uint32_t, bigtime_t);
bigtime_t system_time(void);
#ifdef __cplusplus
}
#endif
#endif

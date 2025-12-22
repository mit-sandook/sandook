#include <stdint.h>  // NOLINT

#if __cplusplus
extern "C" {
#endif

void sandook_init(uint64_t n_sectors);
void sandook_teardown();
void sandook_submit_read(uint64_t sector, uint64_t fnptr, void *cb_arg);
void sandook_submit_write(uint64_t sector, uint64_t fnptr, void *cb_arg);

#if __cplusplus
}
#endif

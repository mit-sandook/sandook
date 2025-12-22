#include "sandook/virtual_disk/bindings.h"

#include <memory>

extern "C" {
#include <cstdint>
}

#include "sandook/base/io_desc.h"
#include "sandook/virtual_disk/loadgen_utils.h"

std::unique_ptr<sandook::LoadGenUtils> loadgen;

extern "C" void sandook_init(uint64_t n_sectors) {
  loadgen = std::make_unique<sandook::LoadGenUtils>(n_sectors);
}

extern "C" void sandook_submit_read(uint64_t sector, uint64_t cb,
                                    void *cb_arg) {
  auto user_cb = (sandook::Callback)cb;
  loadgen->SubmitRead(sector, user_cb, cb_arg);
}

extern "C" void sandook_submit_write(uint64_t sector, uint64_t cb,
                                     void *cb_arg) {
  auto user_cb = (sandook::Callback)cb;
  loadgen->SubmitWrite(sector, user_cb, cb_arg);
}

extern "C" void sandook_teardown() { loadgen.reset(); }

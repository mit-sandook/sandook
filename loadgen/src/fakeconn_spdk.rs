#![cfg(feature = "spdk")]

extern crate shenango;

use shenango::storage::{storage_read, storage_write};
use shenango::WaitGroup;

use std::sync::Arc;

use vdisk_rust::ffi::IOResult;
use vdisk_rust::ffi::IOStatus_kFailed;
use vdisk_rust::ffi::IOStatus_kOk;

pub const SECTOR_SIZE: usize = 4096;

pub struct FakeConnectionSPDK {
    wg: Arc<WaitGroup>,
}

impl FakeConnectionSPDK {
    pub fn new() -> Self {
        FakeConnectionSPDK {
            wg: Arc::new(WaitGroup::new()),
        }
    }

    pub fn submit_spdk_op(&self, sector: u64, fnptr: u64, ptr: *mut u8, op: u8) {
        let idx = ptr as u64;
        let wg = self.wg.clone();

        shenango::thread::spawn_detached(move || {
            wg.add(1);
            let mut buf = vec![0u8; SECTOR_SIZE];
            let resp = match op {
                0 => storage_read(&mut buf, sector),
                1 => storage_write(&buf, sector),
                _ => panic!("Unknown op: {}", op),
            };
            let callback =
                unsafe { std::mem::transmute::<u64, extern "C" fn(u64, IOResult)>(fnptr) };
            callback(
                idx,
                IOResult {
                    status: match resp.is_err() {
                        true => IOStatus_kFailed,
                        false => IOStatus_kOk,
                    },
                    res: resp.unwrap_or(0) as i32,
                },
            );
            wg.done();
        });
    }
}

impl Drop for FakeConnectionSPDK {
    fn drop(&mut self) {
        self.wg.wait();
    }
}

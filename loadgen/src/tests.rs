extern crate vdisk_rust;

use std::{thread, time};
use vdisk_rust::ffi::*;

#[repr(C)]
#[derive(Debug)]
struct Test {
    a: i32,
    b: i32,
}

extern "C" fn test(x: u64, test: IOResult) {
    println!("{:?}", test.status == IOStatus_kOk);
    println!("Hello, world!");
    println!("{:x}", x);
    println!("{:?}", test);
}

fn run() {
    let fnptr = test as usize as u64;
    let mut test_str = Test { a: 1, b: 2 };
    let test_ptr = &mut test_str as *mut _;

    unsafe { sandook_init(10) };
    unsafe { sandook_submit_read(1, fnptr, std::mem::transmute_copy(&test_ptr)) };
    unsafe { sandook_submit_write(1, fnptr, std::mem::transmute_copy(&test_ptr)) };
    unsafe { sandook_teardown() };

    let interval = time::Duration::from_secs(5);
    thread::sleep(interval);
}

fn main() {
    let cfgpath = String::from("../build/sandook/test/bench_apps.config");
    shenango::runtime_init(cfgpath, run).unwrap();
}

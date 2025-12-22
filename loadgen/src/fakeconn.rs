#![allow(non_upper_case_globals)]

use once_cell::sync::Lazy;
use shenango::ffi::timer_sleep;
use std::cell::Cell;
use std::io::{self, Read, Write};
use std::net::SocketAddrV4;
use std::sync::atomic::Ordering::*;
use std::sync::atomic::{AtomicBool, AtomicU16};
use std::sync::RwLock;
use vdisk_rust::ffi::*;

#[cfg(feature = "spdk")]
use crate::fakeconn_spdk::*;

use crate::sandook::PacketHeader;

const MAX_CONNS: u16 = 100;
const CAPACITY_PER_INSTANCE: usize = 1024;

static mut CONNECTION_ID: AtomicU16 = AtomicU16::new(0);

static RX_BUFFER: Lazy<Vec<RwLock<Vec<usize>>>> = Lazy::new(|| {
    let mut root = Vec::with_capacity(MAX_CONNS as usize);
    for _ in 0..MAX_CONNS {
        let v = Vec::with_capacity(CAPACITY_PER_INSTANCE);
        root.push(RwLock::new(v));
    }
    root
});

extern "C" fn callback(idx: u64, status: IOResult) {
    let pkt_ref = unsafe { &mut *(idx as *mut PacketHeader) };
    match status.status {
        IOStatus_kOk => {
            pkt_ref.rx_tsc = shenango::rdtsc();
            RX_BUFFER[pkt_ref.conn_id as usize]
                .write()
                .unwrap()
                .push(pkt_ref.req_handle);
        }
        IOStatus_kFailed => {}
        _ => unreachable!("Unknown status: {}", status.status),
    }
}

pub struct FakeConnection {
    tx_buffer: Box<[Cell<PacketHeader>]>,
    wait: AtomicBool,
    conn_id: u16,

    #[cfg(feature = "spdk")]
    spdk: FakeConnectionSPDK,
}

impl FakeConnection {
    pub fn new(max_packets: usize) -> Self {
        let mut v = Vec::with_capacity(max_packets + 1);
        for _ in 0..v.capacity() {
            v.push(Default::default());
        }
        let raw = v.into_boxed_slice();
        unsafe { assert!(CONNECTION_ID.load(Relaxed) < MAX_CONNS) };

        FakeConnection {
            tx_buffer: raw,
            wait: AtomicBool::new(true),
            conn_id: unsafe { CONNECTION_ID.fetch_add(1, Relaxed) },

            #[cfg(feature = "spdk")]
            spdk: FakeConnectionSPDK::new(),
        }
    }

    pub fn local_addr(&self) -> SocketAddrV4 {
        unimplemented!("FakeConnection::local_addr()");
    }

    pub fn shutdown(&self) {
        self.wait.store(false, Relaxed);
        unsafe { CONNECTION_ID.fetch_sub(1, Relaxed) };
    }
}

impl<'a> Read for &'a FakeConnection {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        while RX_BUFFER[self.conn_id as usize].read().unwrap().is_empty() {
            if !self.wait.load(Relaxed) {
                return Ok(0);
            }
            unsafe {
                timer_sleep(10);
            }
        }

        let index = RX_BUFFER[self.conn_id as usize]
            .write()
            .unwrap()
            .pop()
            .unwrap();
        let packet = self.tx_buffer[index].take();
        let mut temp = Vec::with_capacity(buf.len());
        packet.write(&mut temp).unwrap();
        buf.copy_from_slice(&temp[..]);
        Ok(buf.len())
    }
}
impl Read for FakeConnection {
    fn read(&mut self, _buf: &mut [u8]) -> io::Result<usize> {
        unreachable!();
    }
}
impl<'a> Write for &'a FakeConnection {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let mut hdr = PacketHeader::read(&mut &buf[..]).unwrap();
        let _sector = hdr.lba;
        let _is_read = hdr.opcode == 0;
        let idx = hdr.req_handle;
        hdr.conn_id = self.conn_id;
        hdr.tx_tsc = shenango::rdtsc();
        self.tx_buffer[idx].replace(hdr);

        let ptr = self.tx_buffer[idx].as_ptr();
        // Backend: VDisk
        #[cfg(all(feature = "sandook", not(feature = "spdk")))]
        {
            let fnptr = callback as usize as u64;
            match _is_read {
                true => unsafe {
                    sandook_submit_read(_sector, fnptr, ptr as *mut _);
                },
                false => unsafe {
                    sandook_submit_write(_sector, fnptr, ptr as *mut _);
                },
            }
        }

        // Backend: SPDK
        #[cfg(all(feature = "spdk", not(feature = "sandook")))]
        {
            let fnptr = callback as usize as u64;
            self.spdk
                .submit_spdk_op(_sector, fnptr, ptr as *mut _, !_is_read as u8);
        }

        // Backend: Fake
        #[cfg(all(not(feature = "spdk"), not(feature = "sandook")))]
        {
            callback(ptr as u64, IOResult { status: 0, res: 0 });
        }

        Ok(buf.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        unimplemented!()
    }
}
impl Write for FakeConnection {
    fn write(&mut self, _buf: &[u8]) -> io::Result<usize> {
        unimplemented!()
    }
    fn flush(&mut self) -> io::Result<()> {
        unimplemented!()
    }
}

unsafe impl Send for FakeConnection {}
unsafe impl Sync for FakeConnection {}

use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use clap::Arg;
use dialoguer::Confirm;
use rand::distributions::Uniform;
use rand::Rng;
use rand_distr::Distribution as DistR;
use rand_mt::Mt64;
use std::fs::OpenOptions;
use std::{
    io::{self, Error, ErrorKind, Read},
    sync::atomic::AtomicBool,
    sync::atomic::Ordering::*,
};
#[cfg(feature = "sandook")]
use vdisk_rust::ffi::*;

pub const SANDOOK_HDR_SZ: usize = 42;
const SANDOOK_MAGIC: u16 = SANDOOK_HDR_SZ as u16;

use Buffer;
use Connection;
use Distribution;
use LoadgenProtocol;
use Packet;
use Transport;

enum Opcode {
    Read = 0x00,
    Write = 0x01,
}

#[derive(Default, Debug)]
pub struct PacketHeader {
    pub req_handle: usize,
    pub lba: u64,
    pub lba_count: usize,
    pub opcode: u16,
    pub tx_tsc: u64,

    pub conn_id: u16,
    pub rx_tsc: u64,
}

impl PacketHeader {
    pub fn write<W: io::Write>(self, writer: &mut W) -> io::Result<()> {
        writer.write_u16::<LittleEndian>(SANDOOK_MAGIC)?;
        writer.write_u16::<LittleEndian>(self.opcode)?;
        writer.write_u64::<LittleEndian>(self.req_handle as u64)?;
        writer.write_u64::<LittleEndian>(self.lba)?;
        writer.write_u32::<LittleEndian>(self.lba_count as u32)?;
        writer.write_u64::<LittleEndian>(self.tx_tsc)?;

        writer.write_u16::<LittleEndian>(self.conn_id)?;
        writer.write_u64::<LittleEndian>(self.rx_tsc)?;
        Ok(())
    }

    pub fn read<R: io::Read>(reader: &mut R) -> io::Result<PacketHeader> {
        let magic = reader.read_u16::<LittleEndian>()?;
        if magic != SANDOOK_MAGIC {
            return Err(Error::new(
                ErrorKind::Other,
                format!("Bad magic number in response header: {}", magic),
            ));
        }
        let header = PacketHeader {
            opcode: reader.read_u16::<LittleEndian>()?,
            req_handle: reader.read_u64::<LittleEndian>()? as usize,
            lba: reader.read_u64::<LittleEndian>()?,
            lba_count: reader.read_u32::<LittleEndian>()? as usize,
            tx_tsc: reader.read_u64::<LittleEndian>()?,

            conn_id: reader.read_u16::<LittleEndian>()?,
            rx_tsc: reader.read_u64::<LittleEndian>()?,
        };
        Ok(header)
    }
}

pub struct SandookProtocol {
    _request_size: usize,
    pct_writes: u64,
    interactive: bool,

    nsectors: usize,
    runtime_intialized: AtomicBool,
}

impl SandookProtocol {
    pub fn with_args(matches: &clap::ArgMatches, tport: Transport, _dist: Distribution) -> Self {
        if let Transport::Udp = tport {
            panic!("udp is unsupported by the sandook protocol");
        }
        let nsectors = value_t!(matches, "sandook_nsectors", usize).unwrap();
        SandookProtocol {
            nsectors,
            _request_size: value_t!(matches, "sandook_request_size", usize).unwrap(),
            pct_writes: value_t!(matches, "sandook_set_rate", u64).unwrap(),
            interactive: value_t!(matches, "sandook_interactive", bool).unwrap(),
            runtime_intialized: AtomicBool::new(false),
        }
    }

    // Not in LoadgenProtocol trait. So, we need to call this locally.
    pub fn init_sandook(&self) {
        if self
            .runtime_intialized
            .compare_exchange_weak(false, true, Release, Relaxed)
            .is_ok()
        {
            #[cfg(feature = "sandook")]
            unsafe {
                sandook_init(self.nsectors as u64)
            };
            if self.interactive {
                self.user_input();
            }
        }
    }

    pub fn args<'a, 'b>() -> Vec<clap::Arg<'a, 'b>> {
        vec![
            Arg::with_name("sandook_set_rate")
                .long("sandook_set_rate")
                .takes_value(true)
                .default_value("0")
                .help("Sandook: write requests per 1000 requests"),
            Arg::with_name("sandook_request_size")
                .long("sandook_request_size")
                .takes_value(true)
                .default_value("32")
                .help("Sandook: request size in bytes"),
            Arg::with_name("sandook_nsectors")
                .long("sandook_nsectors")
                .takes_value(true)
                .default_value("2097152")
                .help("Sandook: number of sectors per request"),
            Arg::with_name("sandook_interactive")
                .long("sandook_interactive")
                .takes_value(false)
                .default_value("false")
                .help("Run Sandook in interactive mode"),
        ]
    }

    pub fn user_input(&self) {
        match Confirm::new()
            .with_prompt("Do you want to continue?")
            .interact()
            .unwrap()
        {
            true => (),
            false => panic!("User aborted"),
        }
    }
}

impl LoadgenProtocol for SandookProtocol {
    fn gen_req(&self, i: usize, p: &Packet, buf: &mut Vec<u8>) {
        if !self.runtime_intialized.load(Relaxed) {
            self.init_sandook();
        }
        let mut rng: Mt64 = Mt64::new(p.randomness);
        let lba = rng.gen::<u64>() % (self.nsectors as u64);
        let lbacount = 1;

        let opcode = if Uniform::new(0, 1000).sample(&mut rng) < self.pct_writes {
            Opcode::Write as u16
        } else {
            Opcode::Read as u16
        };

        PacketHeader {
            opcode,
            req_handle: i + 1,
            lba: lba,
            lba_count: lbacount as usize,
            tx_tsc: 0,
            conn_id: 0,
            rx_tsc: 0,
        }
        .write(buf)
        .unwrap();
    }

    fn uses_ordered_requests(&self) -> bool {
        false
    }

    fn read_response(
        &self,
        mut sock: &Connection,
        scratch: &mut Buffer,
    ) -> io::Result<(usize, u64, u64)> {
        let buf = scratch.get_empty_buf();
        sock.read_exact(&mut buf[..SANDOOK_HDR_SZ])?;
        let hdr = PacketHeader::read(&mut &buf[..])?;
        Ok((hdr.req_handle - 1, hdr.tx_tsc, hdr.rx_tsc - hdr.tx_tsc))
    }
}

impl Drop for SandookProtocol {
    fn drop(&mut self) {
        if self
            .runtime_intialized
            .compare_exchange_weak(true, false, Release, Relaxed)
            .is_ok()
        {
            #[cfg(feature = "sandook")]
            unsafe {
                sandook_teardown()
            };
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use test::Bencher;

    pub fn new_sandook() -> SandookProtocol {
        SandookProtocol {
            nsectors: 1024,
            _request_size: 4096,
            pct_writes: 0,
            runtime_intialized: AtomicBool::new(true),
        }
    }

    #[bench]
    // cargo bench --package synthetic --bin synthetic
    fn bench_gen_req(b: &mut Bencher) {
        let mut packet = Packet::default();
        packet.randomness = shenango::rdtsc();
        let sandook_protocol = new_sandook();
        let mut buf: Vec<u8> = Vec::with_capacity(SANDOOK_HDR_SZ);
        let mut i = 0;
        b.iter(|| {
            buf.clear();
            sandook_protocol.gen_req(i, &packet, &mut buf);
            i += 1;
        });
    }
}

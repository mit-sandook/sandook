extern crate clap;
extern crate rand;
extern crate shenango;

use clap::value_t;
use clap::App;
use clap::Arg;

use rand::Rng;
use shenango::storage::*;
use shenango::WaitGroup;
use std::path::PathBuf;
use std::sync::atomic::AtomicU16;
use std::sync::atomic::Ordering::*;
use std::sync::Arc;
use std::sync::RwLock;

#[derive(Clone, Copy)]
enum Op {
    Read,
    Write,
}

struct Worker {
    wg: Arc<WaitGroup>,

    nsectors: usize,
    sector_size: usize,
    write_pct: u64,

    outstanding: u16,
    duration_us: usize,

    op_rng: rand::rngs::ThreadRng,

    start_us: u64,
    end_us: u64,

    next_us: f64,
    delay_us: f64,

    latencies: Arc<RwLock<Vec<u64>>>,
}

impl Worker {
    fn new(
        wg_parent: Arc<WaitGroup>,
        duration: usize,
        rate: usize,
        write_pct: u64,
        outstanding: u16,
    ) -> Self {
        let sector_size = storage_block_size().unwrap();
        Worker {
            wg: wg_parent,

            nsectors: storage_num_blocks().unwrap(),
            sector_size,
            write_pct,

            outstanding,
            duration_us: duration * 1_000_000,
            op_rng: rand::thread_rng(),

            start_us: 0,
            end_us: 0,
            next_us: 0.0,
            delay_us: 1_000_000.0 / rate as f64,

            latencies: Arc::new(RwLock::new(Vec::with_capacity(rate * duration))),
        }
    }

    fn run(&mut self) {
        let outstanding = Arc::new(AtomicU16::new(0));
        self.start_us = shenango::microtime();
        self.next_us = self.start_us as f64 + self.delay_us;

        while shenango::microtime() < self.start_us + self.duration_us as u64 {
            if shenango::microtime() as f64 > self.next_us
                && outstanding.load(SeqCst) < self.outstanding
            {
                let lba = self.op_rng.gen_range(0, self.nsectors) as u64;
                let op = match self.op_rng.gen_range(0, 100) < self.write_pct as usize {
                    true => Op::Write,
                    false => Op::Read,
                };
                let outstanding: Arc<AtomicU16> = outstanding.clone();
                let lat = self.latencies.clone();
                let sector_size = self.sector_size;

                shenango::thread::spawn_detached(move || {
                    outstanding.fetch_add(1, SeqCst);
                    let start = shenango::microtime();
                    let mut buf = vec![0u8; sector_size];
                    match op {
                        Op::Read => storage_read(&mut buf, lba).expect("read failed"),
                        Op::Write => storage_write(&buf, lba).expect("write failed"),
                    };
                    lat.write().unwrap().push(shenango::microtime() - start);
                    outstanding.fetch_sub(1, SeqCst);
                });

                self.next_us += self.delay_us;
            }
        }
        while outstanding.load(Acquire) != 0 {
            println!("waiting for outstanding requests");
            unsafe { shenango::ffi::timer_sleep(1000) };
        }
        self.end_us = shenango::microtime();
        self.print_stats();
        self.wg.done();
    }

    fn print_stats(&self) {
        self.latencies.write().unwrap().sort();
        let len = self.latencies.read().unwrap().len();
        let p50 = self.latencies.read().unwrap()[len / 2];
        let p90 = self.latencies.read().unwrap()[len * 9 / 10];
        let p99 = self.latencies.read().unwrap()[len * 99 / 100];
        println!(
            "IOPS: {}",
            len as f64 / (self.end_us - self.start_us) as f64 * 1000000.0
        );
        println!("p50: {}", p50);
        println!("p90: {}", p90);
        println!("p99: {}", p99);
    }
}

fn work_handler(
    wg_parent: Arc<WaitGroup>,
    duration: usize,
    rate: usize,
    write_pct: u64,
    outstanding: u16,
) {
    let wg = Arc::new(WaitGroup::new());
    wg.add(1_i32);
    let wg2 = wg.clone();
    shenango::thread::spawn_detached(move || {
        let mut worker = Worker::new(wg2, duration, rate, write_pct, outstanding);
        worker.run();
    });
    shenango::thread::thread_yield();
    wg.wait();
    wg_parent.done();
}

fn main_handler() {
    let matches = App::new("SPDK Workload Generator")
        .version("0.1")
        .arg(
            Arg::with_name("duration")
                .short("d")
                .long("duration")
                .default_value("10")
                .help("Amount of time to run the workload in seconds"),
        )
        .arg(
            Arg::with_name("rate")
                .short("r")
                .long("rate")
                .default_value("100000")
                .help("Rate of requests per second"),
        )
        .arg(
            Arg::with_name("write_pct")
                .short("w")
                .long("write_pct")
                .default_value("0")
                .help("Percentage of requests that are writes"),
        )
        .arg(
            Arg::with_name("outstanding")
                .short("o")
                .long("max_outstanding")
                .default_value("256")
                .help("Maximum number of outstanding requests"),
        )
        .get_matches();

    let duration = value_t!(matches, "duration", usize).unwrap();
    let rate = value_t!(matches, "rate", usize).unwrap();
    let write_pct = value_t!(matches, "write_pct", u64).unwrap();
    let outstanding = value_t!(matches, "outstanding", u16).unwrap();

    let wg = Arc::new(WaitGroup::new());
    wg.add(1_i32);
    work_handler(wg.clone(), duration, rate, write_pct, outstanding);
    wg.wait();
}

fn main() {
    let manifest_path: PathBuf = std::env::var("CARGO_MANIFEST_DIR")
        .unwrap()
        .parse()
        .unwrap();
    let cfgpath = manifest_path
        .join("../build/sandook/disk_server/disk_server_spdk.config")
        .to_str()
        .unwrap()
        .to_string();
    shenango::runtime_init(cfgpath, main_handler).unwrap();
}

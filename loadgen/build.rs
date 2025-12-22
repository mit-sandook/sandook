fn main() {
    println!("cargo:rustc-flags=-L ../lib/caladan/rdma-core/build/ccan");
    println!("cargo:rustc-flags=-L ../lib/caladan/rdma-core/build/util");
    println!("cargo:rustc-link-arg=-T../lib/caladan/base/base.ld");
}

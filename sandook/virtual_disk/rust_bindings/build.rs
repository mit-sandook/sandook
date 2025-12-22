extern crate bindgen;
extern crate build_deps;

use std::env;
use std::path::PathBuf;
use std::process::Command;

fn link_caladan() {
    build_deps::rerun_if_changed_paths("../../../lib/caladan/inc/**").unwrap();
    build_deps::rerun_if_changed_paths("../../../lib/caladan/inc/*.a").unwrap();

    // Tell cargo to tell rustc to link the library.
    println!("cargo:rustc-link-lib=static=base");
    println!("cargo:rustc-link-lib=static=net");
    println!("cargo:rustc-link-lib=static=runtime");
    let manifest_path: PathBuf = std::env::var("CARGO_MANIFEST_DIR")
        .unwrap()
        .parse()
        .unwrap();
    // the parent/parent is bindings/rust
    let lib_search_path = manifest_path.join("../../../").join("lib/caladan");
    println!("cargo:rustc-flags=-L {}", lib_search_path.to_str().unwrap());
    let link_script_path = lib_search_path.join("base/base.ld");
    println!(
        "cargo:rustc-link-arg=-T{}",
        link_script_path.to_str().unwrap()
    );

    // consult shared.mk for other libraries... sorry y'all.
    let output = Command::new("make")
        .args([
            "-f",
            "../../../lib/caladan/Makefile",
            "print-RUNTIME_LIBS",
            "ROOT_PATH=../../../lib/caladan/",
        ])
        .output()
        .unwrap();
    for t in String::from_utf8_lossy(&output.stdout).split_whitespace() {
        if t.starts_with("-L") {
            println!("cargo:rustc-flags={}", t.replace("-L", "-L "));
        } else if t == "-lmlx5" || t == "-libverbs" || t.contains("spdk") {
            println!("cargo:rustc-link-lib=static={}", t.replace("-l", ""));
        } else if t.starts_with("-l:lib") {
            println!(
                "cargo:rustc-link-lib=static={}",
                t.replace("-l:lib", "").replace(".a", "")
            );
        } else if t == "-lpthread" {
        } else if t.starts_with("-l") {
            println!("cargo:rustc-link-lib={}", t.replace("-l", ""));
        }
    }
}

fn link_virtual_disk() {
    build_deps::rerun_if_changed_paths("../**").unwrap();
    build_deps::rerun_if_changed_paths("../../../build/sandook/*.a").unwrap();

    println!("cargo:rustc-link-lib=static=stdc++");
    println!("cargo:rustc-link-lib=static=virtual_disk");
    println!("cargo:rustc-link-lib=static=config");
    println!("cargo:rustc-link-lib=static=rpc");
    println!("cargo:rustc-link-lib=static=sandook_base");
    println!("cargo:rustc-link-lib=static=mem");
    println!("cargo:rustc-link-lib=static=utils");
    println!("cargo:rustc-link-lib=static=sandook_bindings");
    println!("cargo:rustc-link-lib=jsoncpp");

    println!("cargo:rustc-flags=-L /usr/lib/gcc/x86_64-linux-gnu/13/");

    let manifest_path: PathBuf = std::env::var("CARGO_MANIFEST_DIR")
        .unwrap()
        .parse()
        .unwrap();
    let sandook_root_path = manifest_path.join("../../../");
    let lib_paths = [
        "build/sandook/base",
        "build/sandook/config",
        "build/sandook/rpc",
        "build/sandook/mem",
        "build/sandook/utils",
        "build/sandook/bindings",
        "build/sandook/virtual_disk",
    ];

    for lib_path in lib_paths.iter() {
        let lib_search_path = sandook_root_path.join(lib_path);
        println!("cargo:rustc-flags=-L {}", lib_search_path.to_str().unwrap());
    }
}

fn main() {
    link_caladan();
    link_virtual_disk();

    // Generate bindings
    let bindings = bindgen::Builder::default()
        .header("sandook.h")
        .clang_args(&[
            "-I../../virtual_disk/",
            "-I../../config/",
            "-I../../rpc/",
            "-I../../base/",
            "-I../../mem/",
            "-I../../bindings/",
        ])
        .blocklist_function("q.cvt(_r)?")
        .blocklist_function("strtold")
        .generate_comments(false)
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

use std::{env::var, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=src/wrapper.h");
    println!("cargo:rustc-link-lib=numa");

    let out_dir = PathBuf::from(var("OUT_DIR").unwrap());

    bindgen::builder()
        .header("src/wrapper.h")
        .layout_tests(false)
        .generate()
        .unwrap()
        .write_to_file(out_dir.join("bindings.rs"))
        .unwrap();
}

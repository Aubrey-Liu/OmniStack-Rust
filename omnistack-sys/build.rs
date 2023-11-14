fn main() {
    println!("cargo:rustc-link-lib=numa");
    pkg_config::probe_library("libdpdk").unwrap();
}

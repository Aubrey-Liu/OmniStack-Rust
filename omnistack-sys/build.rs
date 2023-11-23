fn main() {
    let libs = ["src/dpdk/packet.c"];

    for l in libs {
        println!("cargo:rerun-if-changed={l}");
    }

    println!("cargo:rustc-link-lib=numa");
    pkg_config::probe_library("libdpdk").unwrap();

    cc::Build::new()
        .files(libs)
        .include("src")
        .flag_if_supported("-mssse3")
        .compile("omnistack-sys-lib");

    bindgen::builder()
        .header("src/dpdk/packet.c")
        .allowlist_function("pktpool_create")
        .allowlist_function("pktpool_alloc")
        .allowlist_function("pktpool_dealloc")
        .layout_tests(false)
        .generate()
        .unwrap()
        .write_to_file("src/dpdk/packet.rs")
        .unwrap();
}

fn main() {
    let libs = ["src/dpdk/packet.c", "src/dpdk/device.c"];

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
        .header("src/dpdk/device.c")
        .allowlist_function("pktpool.*")
        .allowlist_function("port_init")
        .layout_tests(false)
        .generate()
        .unwrap()
        .write_to_file("src/dpdk/bindings.rs")
        .unwrap();
}

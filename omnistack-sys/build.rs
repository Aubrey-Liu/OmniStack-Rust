fn main() {
    let libs = ["src/dpdk/packet.c", "src/dpdk/device.c", "src/dpdk/utils.c"];

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
        .header(libs[0])
        .header(libs[1])
        .header(libs[2])
        .allowlist_file(libs[2])
        .allowlist_function("pktpool.*")
        .allowlist_function("mempool.*")
        .allowlist_function("dev.*")
        .layout_tests(false)
        .generate()
        .unwrap()
        .write_to_file("src/dpdk/bindings.rs")
        .unwrap();
}

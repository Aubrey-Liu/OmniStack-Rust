fn main() {
    let c_files = ["src/modules/dpdk/dpdk.c"];

    for p in c_files {
        println!("cargo:rerun-if-changed={p}");
    }

    println!("cargo:rustc-link-lib=numa");
    pkg_config::probe_library("libdpdk").unwrap();

    cc::Build::new()
        .files(c_files)
        .include("src")
        .compile("omnistack-clib");
}

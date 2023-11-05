const SRC_PATH: &'static str = "../src/";

fn main() {
    let inc_paths = [
        "data_plane/include",
        "libraries/channel/include",
        "libraries/memory/include",
        "libraries/hashtable/include",
        "libraries/node/include",
        "libraries/packet/include",
        "libraries/token/include",
        "libraries/common/include",
        "libraries/socket/include",
    ]
    .iter()
    .map(|p| SRC_PATH.to_string() + p)
    .map(|p| std::path::PathBuf::from(p));

    let compiled_files = [
        "libraries/memory/common.cpp",
        "libraries/channel/channel.cpp",
    ]
    .iter()
    .map(|p| SRC_PATH.to_string() + p)
    .map(|p| std::path::PathBuf::from(p));

    #[cfg(feature = "dpdk")]
    let mut b = autocxx_build::Builder::new("src/lib.rs", inc_paths)
        .extra_clang_args(&["-std=c++20", "-D OMNIMEM_BACKEND_DPDK"])
        .build()
        .unwrap();

    #[cfg(not(feature = "dpdk"))]
    let mut b = autocxx_build::Builder::new("src/lib.rs", inc_paths)
        .extra_clang_args(&["-std=c++20"])
        .build()
        .unwrap();

    #[cfg(feature = "dpdk")]
    b.flag_if_supported("-std=c++2a")
        .files(compiled_files)
        .define("OMNIMEM_BACKEND_DPDK", None)
        .compile("omnistack-core");

    #[cfg(not(feature = "dpdk"))]
    b.flag_if_supported("-std=c++2a")
        .files(compiled_files)
        .compile("omnistack-core");

    println!("cargo:rustc-link-lib=numa");
    pkg_config::probe_library("libdpdk").unwrap();
    pkg_config::probe_library("jsoncpp").unwrap();
}

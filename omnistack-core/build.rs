const SRC_PATH: &'static str = "../src/";

fn main() {
    let inc_paths = [
        "libraries/memory/include",
        "libraries/common/include",
        // "libraries/channel/include",
        // "libraries/hashtable/include",
        // "libraries/node/include",
        // "libraries/packet/include",
        // "libraries/token/include",
        // "libraries/socket/include",
        // "data_plane/include",
    ]
    .iter()
    .map(|p| format!("{}{}", SRC_PATH, p))
    .map(|p| std::path::PathBuf::from(p));

    let compiled_files = ["libraries/memory/common.cpp"]
        .iter()
        .map(|p| format!("{}{}", SRC_PATH, p))
        .map(|p| std::path::PathBuf::from(p));

    let mut clang_flags = vec!["-std=c++20"];
    #[cfg(feature = "dpdk")]
    clang_flags.push("-D OMNIMEM_BACKEND_DPDK");

    let mut b = autocxx_build::Builder::new("src/lib.rs", inc_paths.clone())
        .extra_clang_args(&clang_flags)
        .build()
        .unwrap();

    let b = b
        .flag_if_supported("-std=c++2a")
        .opt_level(2)
        .files(compiled_files.clone());
    #[cfg(feature = "dpdk")]
    let b = b.define("OMNIMEM_BACKEND_DPDK", None);
    b.warnings(false).compile("omnistack-core");

    println!("cargo:rustc-link-lib=numa");
    pkg_config::probe_library("libdpdk").unwrap();
    pkg_config::probe_library("jsoncpp").unwrap();
}

/// Register a module with its identifier and builder.
/// The identifier can be user-costomized, or by default, the type's name.
#[macro_export]
macro_rules! register_module {
    ($ty:ty, $buildfn:path) => {
        $crate::paste! {
            #[allow(non_snake_case)]
            #[$crate::constructor(65535)]
            extern "C" fn [<__register_ $ty>]() {
                $crate::module_utils::register(stringify!($ty), $buildfn);
            }
        }
    };
}

// #[macro_export]
// macro_rules! module_cbindgen {
//     ($ty:ty, $prefix:ident) => {
//         mod ffi {
//             extern "C" {
//                 $crate::paste! {
//                     #[allow(unused, improper_ctypes)]
//                     #[no_mangle]
//                     pub(crate) fn [<$prefix _init>](
//                         module: *mut super::$ty,
//                     ) -> ::std::ffi::c_int;
//                 }

//                 $crate::paste! {
//                     #[allow(unused, improper_ctypes)]
//                     #[no_mangle]
//                     pub(crate) fn [<$prefix _process>](
//                         module: *mut super::$ty,
//                         ctx: *const $crate::engine::Context,
//                         packet: *mut $crate::packet::Packet,
//                     ) -> ::std::ffi::c_int;
//                 }

//                 $crate::paste! {
//                     #[allow(unused, improper_ctypes)]
//                     #[no_mangle]
//                     pub(crate) fn [<$prefix _tick>](
//                         module: *mut super::$ty,
//                         ctx: *const $crate::engine::Context,
//                         packet: *mut $crate::packet::Packet,
//                     ) -> ::std::ffi::c_int;
//                 }
//             }
//         }
//     };
// }

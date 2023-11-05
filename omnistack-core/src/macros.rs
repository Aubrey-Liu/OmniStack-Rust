/// Register a module with its identifier and builder.
/// The identifier can be user-costomized, or by default, the type's name.
#[macro_export]
macro_rules! register_module {
    ($ty:ident, $buildfn:expr) => {
        $crate::concat_idents!(fn_name = __register_, $ty {
            #[allow(non_snake_case)]
            #[$crate::ctor]
            fn fn_name() {
                $crate::modules::registry::register(stringify!($ty), $buildfn);
            }
        });
    };
    ($ty:ident, $buildfn:expr, $id:expr) => {
        $crate::concat_idents!(fn_name = __register_, $id {
            #[allow(non_snake_case)]
            #[$crate::ctor]
            fn fn_name() {
                $crate::modules::registry::register($id, $buildfn);
            }
        });
    };
}

/// Register a module with its identifier and builder.
/// The identifier can be user-costomized, or by default, the type's name.
#[macro_export]
macro_rules! register_module {
    ($ty:ident, $buildfn:expr) => {
        $crate::concat_idents!(fn_name = __register_, $ty {
            #[allow(non_snake_case)]
            #[$crate::constructor(65535)]
            extern "C" fn fn_name() {
                $crate::modules::factory::register(stringify!($ty), $buildfn);
            }
        });
    };
}

#[macro_export]
macro_rules! register_module_to {
    ($ty:ident, $builder:path, $register:path) => {
        $crate::paste! {
            #[$crate::constructor(65535)]
            extern "C" fn [<__register $ty:lower>]() {
                $register(stringify!($ty), $builder);
            }
        }
    };
}

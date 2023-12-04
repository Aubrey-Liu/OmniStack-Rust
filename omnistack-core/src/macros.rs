#[macro_export]
macro_rules! register_module_to {
    ($ty:ident, $builder:path, $register:path) => {
        $crate::paste! {
            #[allow(non_snake_case)]
            #[$crate::constructor(65535)]
            extern "C" fn [<__register_ $ty>]() {
                $register(stringify!($ty), $builder);
            }
        }
    };
}

/// Register a module with its identifier and builder.
/// The identifier can be user-costomized, or by default, the type's name.
#[macro_export]
macro_rules! register_module {
    ($ty:ident) => {
        $crate::register_module_to!($ty, $ty::new, $crate::module::register);
    };

    ($ty:ident, $builder:path) => {
        $crate::register_module_to!($ty, $builder, $crate::module::register);
    };
}

#[macro_export]
macro_rules! register_adapter {
    ($ty:ident) => {
        $crate::register_module_to!($ty, $ty::new, $crate::io_module::register);
    };

    ($ty:ident, $builder:path) => {
        $crate::register_module_to!($ty, $builder, $crate::io_module::register);
    };
}

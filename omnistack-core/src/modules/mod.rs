use std::collections::HashMap;
use std::sync::Mutex;

use once_cell::sync::Lazy;

#[macro_export]
macro_rules! register_module {
    ($ty:ident, $builder:expr) => {
        ::concat_idents::concat_idents!(fn_name = __register_, $ty {
            #[allow(non_snake_case)]
            #[::ctor::ctor]
            fn fn_name() {
                crate::modules::register(stringify!($ty), $builder);
            }
        });
    };
    ($ty:ident, $builder:expr, $name:expr) => {
        ::concat_idents::concat_idents!(fn_name = __register_, $name {
            #[allow(non_snake_case)]
            #[::ctor::ctor]
            fn fn_name() {
                crate::modules::register(stringify!($ty), $builder);
            }
        });
    };
}

pub trait Module {}

type ModuleBuildFn = fn() -> Box<dyn Module>;

pub fn register(ty_name: &'static str, builder: ModuleBuildFn) {
    assert!(
        MODULES.lock().unwrap().insert(ty_name, builder).is_none(),
        "duplicate module names"
    );
}

static MODULES: Lazy<Mutex<HashMap<&'static str, ModuleBuildFn>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_register_module() {
        pub struct FooMod;
        impl FooMod {
            fn new() -> Box<dyn Module> {
                Box::new(FooMod)
            }
        }
        impl Module for FooMod {}

        register_module!(FooMod, FooMod::new);

        MODULES
            .lock()
            .unwrap()
            .get("FooMod")
            .expect("FooMod should be registered");
    }
}

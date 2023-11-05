use std::collections::HashMap;
use std::sync::Mutex;

use colored::Colorize;
use once_cell::sync::Lazy;

/// Register a module with its identifier and builder.
/// The identifier can be user-costomized, or by default, the type's name.
#[macro_export]
macro_rules! register_module {
    ($ty:ident, $buildfn:expr) => {
        ::concat_idents::concat_idents!(fn_name = __register_, $ty {
            #[allow(non_snake_case)]
            #[::ctor::ctor]
            fn fn_name() {
                crate::modules::registry::register(stringify!($ty), $buildfn);
            }
        });
    };
    ($ty:ident, $buildfn:expr, $id:expr) => {
        ::concat_idents::concat_idents!(fn_name = __register_, $id {
            #[allow(non_snake_case)]
            #[::ctor::ctor]
            fn fn_name() {
                crate::modules::registry::register($id, $buildfn);
            }
        });
    };
}

pub trait Module {}

type ModuleBuildFn = fn() -> Box<dyn Module>;

/// Register a module with its identifier and builder.
///
/// Normally users should not use this method directly,
/// and please use [`register_module`] instead.
pub fn register(id: &'static str, f: ModuleBuildFn) {
    if MODULES.lock().unwrap().insert(id, f).is_some() {
        println!(
            "{}: Module '{}' has already been registered",
            "warning".bright_yellow().bold(),
            id
        );
    }
}

static MODULES: Lazy<Mutex<HashMap<&'static str, ModuleBuildFn>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_register_module() {
        pub struct Foo;
        impl Foo {
            fn new() -> Box<dyn Module> {
                Box::new(Foo)
            }
        }
        impl Module for Foo {}

        register_module!(Foo, Foo::new);
        // register_module!(Foo, Foo::new);  // can't register twice; shouldn't compile

        MODULES
            .lock()
            .unwrap()
            .get("Foo")
            .expect("Foo should be registered");
    }
}

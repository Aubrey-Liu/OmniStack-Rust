use std::collections::HashMap;
use std::sync::Mutex;

use colored::Colorize;
use once_cell::sync::Lazy;

pub trait Module {}

type ModuleBuildFn = fn() -> Box<dyn Module>;

/// Register a module with its identifier and builder.
///
/// Normally users should not use this method directly,
/// and please use [`register_module`] instead.
pub fn register(id: &'static str, f: ModuleBuildFn) {
    // println!(
    //     "{}: Module '{}' is registered",
    //     "info".bright_green().bold(),
    //     id
    // );
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
    use crate::prelude::*;

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
        register_module!(Foo, Foo::new, "Foo2");

        let m = MODULES.lock().unwrap();

        m.get("Foo").expect("Foo should be registered");
        m.get("Foo2").expect("Foo2 should be registered");
    }
}

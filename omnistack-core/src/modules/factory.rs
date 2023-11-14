use std::collections::HashMap;

use colored::Colorize;
use once_cell::sync::Lazy;

use super::{Module, ModuleId};

type ModuleBuildFn = dyn Fn() -> Box<dyn Module>;

pub(crate) static mut MODULE_FACTORY: Lazy<HashMap<&'static str, Box<ModuleBuildFn>>> =
    Lazy::new(HashMap::new);

pub(crate) static mut MODULES: Lazy<HashMap<ModuleId, Box<dyn Module>>> =
    Lazy::new(HashMap::new);

/// Register a module with its identifier and builder.
///
/// Normally users should not use this method directly,
/// and please use [`register_module`] instead.
pub fn register<T, F>(id: &'static str, f: F)
where
    T: Module + 'static,
    F: Fn() -> T + 'static,
{
    // println!(
    //     "{}: Module '{}' is registered",
    //     "info".bright_green().bold(),
    //     id
    // );
    let f = move || -> Box<dyn Module> {
        let m = f();
        Box::new(m)
    };

    if unsafe { MODULE_FACTORY.insert(id, Box::new(f)).is_some() } {
        println!(
            "{}: Module '{}' has already been registered",
            "warning".bright_yellow().bold(),
            id
        );
    }
}

pub fn build_module(name: &str) -> ModuleId {
    let m = unsafe { MODULE_FACTORY.get(&name).unwrap()() };
    let id = unsafe { MODULES.len() };
    unsafe { MODULES.insert(id, m) };
    id
}

#[inline]
pub fn get_module(id: ModuleId) -> Option<&'static dyn Module> {
    unsafe { MODULES.get(&id).map(|m| m.as_ref()) }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::prelude::*;

    #[test]
    fn test_register_module() {
        pub struct Foo;
        impl Foo {
            fn new() -> Self {
                Foo
            }
        }
        impl Module for Foo {}

        register_module!(Foo, Foo::new);
        // register_module!(Foo, Foo::new);  // can't register twice; shouldn't compile

        unsafe {
            MODULE_FACTORY.get("Foo").expect("Foo should be registered");
        }
    }
}

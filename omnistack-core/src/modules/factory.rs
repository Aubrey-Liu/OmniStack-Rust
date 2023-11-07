use std::collections::HashMap;

use colored::Colorize;
use static_init::dynamic;

pub type PacketId = usize;

pub struct Packet {
    pub id: usize,
}

pub trait Module {
    unsafe fn process(&self, packet: *mut Packet) {
        dbg!(packet.as_ref().unwrap().id);
    }
}

pub type ModuleTy = Box<dyn Module + Send>;
type ModuleBuildFn = fn() -> ModuleTy;

#[dynamic]
pub(crate) static mut MODULE_FACTORY: HashMap<&'static str, ModuleBuildFn> = HashMap::new();

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
    if MODULE_FACTORY.write().insert(id, f).is_some() {
        println!(
            "{}: Module '{}' has already been registered",
            "warning".bright_yellow().bold(),
            id
        );
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::prelude::*;

    #[test]
    fn test_register_module() {
        pub struct Foo;
        impl Foo {
            fn new() -> ModuleTy {
                Box::new(Foo)
            }
        }
        impl Module for Foo {}

        register_module!(Foo, Foo::new);
        // register_module!(Foo, Foo::new);  // can't register twice; shouldn't compile
        register_module!(Foo, Foo::new, "Foo2");

        let m = MODULE_FACTORY.read();

        m.get("Foo").expect("Foo should be registered");
        m.get("Foo2").expect("Foo2 should be registered");
    }
}

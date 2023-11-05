use omnistack_core::prelude::*;

struct Bar;

impl Module for Bar {}

impl Bar {
    fn new() -> Box<dyn Module> {
        Box::new(Bar)
    }
}

register_module!(Bar, Bar::new);

fn main() {
    println!("hello world");
}

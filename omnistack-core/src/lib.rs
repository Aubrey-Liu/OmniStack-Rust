mod macros;
pub mod modules;
pub mod prelude;
pub mod memory;
pub mod engine;

pub use concat_idents::concat_idents;
pub use static_init::constructor;

use autocxx::prelude::*;

include_cpp! {
    #include "omnistack/memory/memory.h"
    safety!(unsafe)
    generate_ns!("omnistack::memory")
}

pub mod engine;
pub mod error;
pub mod io_module;
pub mod macros;
pub mod module;
pub mod packet;
pub mod prelude;
pub mod headers;

pub use paste::paste;
pub use static_init::constructor;

pub type Result<T> = std::result::Result<T, crate::error::Error>;

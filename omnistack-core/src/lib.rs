#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::len_without_is_empty)]

pub mod config;
pub mod engine;
pub mod macros;
pub mod memory;
pub mod module;
pub mod packet;
pub mod prelude;
pub mod protocols;

pub use paste::paste;
pub use static_init::constructor;

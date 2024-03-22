#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::len_without_is_empty)]
#![allow(clippy::missing_safety_doc)]

pub mod channel;
pub mod config;
pub mod engine;
pub mod error;
pub mod macros;
pub mod memory;
pub mod module;
pub mod modules;
pub mod nio;
pub mod packet;
pub mod prelude;
pub mod protocol;
pub mod service;
pub mod sys;
pub mod user;

pub use paste::paste;
pub use static_init::constructor;

pub type Result<T> = std::result::Result<T, crate::error::Error>;

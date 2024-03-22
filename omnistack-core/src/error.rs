use thiserror::Error;

// NOTE: some error types are just here to indicate an event or situation.
// They are not actually errors.
#[derive(Debug, Error)]
pub enum Error {
    #[error("nothing happened")]
    Nop,

    #[error("packet was dropped")]
    Dropped,

    #[error("memory error")]
    MemoryError(#[from] crate::memory::MemoryError),

    #[error("unknown error happened")]
    Unknown,

    #[error("service error")]
    ServiceError(#[from] crate::service::ServiceError),

    #[error("io error")]
    IoError(#[from] std::io::Error),

    #[error("json parse error")]
    JsonParseError(#[from] serde_json::Error),
}

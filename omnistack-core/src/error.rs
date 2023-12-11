use thiserror::Error;

// todo: design errors
#[derive(Debug, Error)]
pub enum Error {
    #[error("failed to init dpdk")]
    DpdkInitErr,

    #[error("dst has no matched route")]
    InvalidDest,

    #[error("no data is present")]
    NoData,

    #[error("unknown errors")]
    Unknown,
}


bench-tx:
	cargo build -r -F txonly,perf
	RUST_LOG=info sudo -E ./target/release/omnistack

debug-tx:
	cargo build -r -F txonly,perf
	RUST_LOG=debug sudo -E ./target/release/omnistack

bench-rx:
	cargo build -r -F rxonly,perf
	RUST_LOG=info sudo -E ./target/release/omnistack

debug-rx:
	cargo build -r -F rxonly,perf
	RUST_LOG=debug sudo -E ./target/release/omnistack

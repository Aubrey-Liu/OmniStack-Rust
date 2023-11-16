bindgen:
	cd omnistack-core/; cargo expand > tmp; \
	cbindgen tmp --output src/bindings.h; rm -f tmp;

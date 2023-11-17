bindgen:
	cd omnistack-core/; cargo expand > tmp; \
	cbindgen tmp -l C --output src/bindings.h; rm -f tmp;

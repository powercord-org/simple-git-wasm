# Remove useless instrumentation from the binary. Saves 200KB+. Runs faster. Worth.
ASYNCIFY_NO="[ \
	'git_threadstate*', \
	'git_buf*', \
	'git_config*', \
	'git_filter*', \
	'git_commit*', \
	'git_object*', \
	'git_odb*', \
	'git_pack*', \
	'git_vector*', \
	'git_index*', \
	'git_pool*', \
	'git_cached*', \
	'git_repository*', \
	'xdl_*' \
]"

EMCC_FLAGS= --post-js src/interface.js \
	 -I libgit2/include \
	 -s MODULARIZE \
	 -s ASYNCIFY \
	 -s ASYNCIFY_IMPORTS=_emhttp_js_read \
	 -s ASYNCIFY_REMOVE=$(ASYNCIFY_NO) \
	 -s EXPORTED_RUNTIME_METHODS=FS,writeArrayToMemory,ccall \
	 -s ENVIRONMENT=node \
	 -s ALLOW_MEMORY_GROWTH \
	 -s SUPPORT_LONGJMP=0 \
	 -lnodefs.js \
	 -o src/wasm/libgit.js \
	 libgit2/build/libgit2.a \
	 src/lib/main.c

EMCC_DEBUG_FLAGS= $(EMCC_FLAGS) \
   -O0 -g3 \
	 -s LLD_REPORT_UNDEFINED \
	 -s STACK_OVERFLOW_CHECK=2 \
	 -s ASSERTIONS=2 \
	 -s SAFE_HEAP

EMCC_BUILD_FLAGS= $(EMCC_FLAGS) \
	 -Oz -flto -g0 \
	 -s LLD_REPORT_UNDEFINED \
	 -s STACK_OVERFLOW_CHECK=2 \
	 -s ASSERTIONS=2 \
	 -s SAFE_HEAP

libgit2/build/libgit2.a:
	# Work folder
	mkdir libgit2/build || true

	# Some patching
	rm libgit2/src/transports/http.c || true
	ln -s ../../../src/lib/http.c libgit2/src/transports/http.c
	sed -i "s/_FILE_MODE 0444/_FILE_MODE 0644/g" libgit2/src/pack.h libgit2/src/odb.h # Permission issues with NODEFS

	# Build the lib
	cd libgit2/build && emcmake cmake \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS="-Oz" \
		-DUSE_HTTPS=OFF \
		-DUSE_SSH=OFF \
		-DTHREADSAFE=OFF \
		-DREGEX_BACKEND=builtin \
		..
	cd libgit2/build && emmake make -j

build: libgit2/build/libgit2.a
	rm -rf src/wasm && mkdir src/wasm
	emcc $(EMCC_BUILD_FLAGS)

debug: libgit2/build/libgit2.a
	rm -rf src/wasm && mkdir src/wasm
	emcc $(EMCC_DEBUG_FLAGS)

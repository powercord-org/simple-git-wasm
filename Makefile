##
# Copyright (c) 2021 Cynthia K. Rey, All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

EMCC_BUILD_FLAGS= $(EMCC_FLAGS)  -Oz -flto -g0

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

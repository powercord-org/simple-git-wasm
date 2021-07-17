/*
 * Copyright (c) 2021 Cynthia K. Rey, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Interface to let the wasm binary use http/https bundled with Node

const connections = new Map()
const deferred = new Map()
let connectionId = 0
let deferredId = 0

WasmModule['createConnection'] = function (url, isPost) {
  const connId = connectionId++
  const client = url.startsWith('https') ? require('https') : require('http')
  const headers = {}
  let method = 'GET'

  if (isPost) {
    method = 'POST'
    headers['content-type'] = url.indexOf('git-upload-pack') > 0
      ? 'application/x-git-upload-pack-request'
      : 'application/x-git-receive-pack-request'
  }

  const req = client.request(url, { method: method, headers: headers })
  connections.set(connId, { req: req, res: null })
  return connId;
}

WasmModule['writeToConnection'] = function (connId, bufferPtr, length) {
  const conn = connections.get(connId)
  if (!conn) return -1

  const buffer = GROWABLE_HEAP_U8().slice(bufferPtr, bufferPtr + length)
  conn.req.write(buffer)
  return 0
}

WasmModule['readFromConnection'] = async function (connId, bufferPtr, length, outPtr) {
  const conn = connections.get(connId)
  if (!conn) return -1

  // [Cynthia] We don't have to worry about concurrent calls here, as this is
  // called by C code which is synchronous and paused until this call completes.
  if (!conn.res) {
    conn.req.end()
    conn.req.on('response', (res) => (conn.res = res))
  }

  let buf
  // Emscripten doesn't support optional chaining
  while (!(buf = conn.res && conn.res.read(length))) await new Promise((resolve) => setImmediate(resolve))
  WasmModule.writeArrayToMemory(buf, bufferPtr)
  Atomics.store(GROWABLE_HEAP_I32(), outPtr >> 2, buf.length)
}

WasmModule['freeConnection'] = function (connId) {
  const conn = connections.get(connId)
  if (conn) {
    connections.delete(connId)
    // Silence any error that may arise from the destroy call
    conn.req.on('error', () => void 0)
    conn.req.destroy()
  }
}

WasmModule['allocString'] = function (str) {
  const length = WasmModule.lengthBytesUTF8(str) + 1
  const ptr = WasmModule._malloc(length)
  WasmModule.stringToUTF8(str, ptr, length)
  return ptr
}

WasmModule['allocDeferred'] = function () {
  let resolve
  const ptr = deferredId++
  const promise = new Promise((r) => (resolve = r))
  deferred.set(ptr, resolve)
  return [ promise, ptr ]
}

WasmModule['invokeDeferred'] = function (ptr, ...args) {
  const resolve = deferred.get(ptr)
  if (!resolve) throw new Error('segmentation fault')
  deferred.delete(ptr)
  resolve(...args)
}

WasmModule['freeDeferred'] = function (ptr) {
  return deferred.delete(ptr)
}

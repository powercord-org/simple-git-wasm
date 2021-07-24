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

// eslint-disable-next-line no-unused-vars
function allocString (str) {
  const length = lengthBytesUTF8(str) + 1
  const ptr = _malloc(length)
  stringToUTF8(str, ptr, length)
  return ptr
}

let deferredId = 0
const deferredMap = new Map()
// eslint-disable-next-line no-unused-vars
function allocDeferred () {
  let resolve
  const ptr = deferredId++
  const promise = new Promise((r) => (resolve = r))
  deferredMap.set(ptr, resolve)
  return [ promise, ptr ]
}

// eslint-disable-next-line no-unused-vars
function invokeDeferred (ptr, ...args) {
  const resolve = deferredMap.get(ptr)
  if (!resolve) throw new Error('segmentation fault')
  deferredMap.delete(ptr)
  resolve(...args)
}

// eslint-disable-next-line no-unused-vars
function freeDeferred (ptr) {
  return deferredMap.delete(ptr)
}

let arrayId = 0
const arrayMap = new Map()
// eslint-disable-next-line no-unused-vars
function allocArray () {
  const array = []
  const ptr = arrayId++
  arrayMap.set(ptr, array)
  return [ array, ptr ]
}

// eslint-disable-next-line no-unused-vars
function arrayPush (ptr, ...data) {
  const array = arrayMap.get(ptr)
  if (!array) throw new Error('segmentation fault')
  array.push(...data)
}

// eslint-disable-next-line no-unused-vars
function freeArray (ptr) {
  return arrayMap.delete(ptr)
}

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

const { resolve, join } = require('path')
const { existsSync } = require('fs')
const { readFile } = require('fs/promises')

const LINE_RE = /\[([^ \]]+)(?: "([^"]+)")?]/i
function parseGitConfig (blob) {
  const cfg = {};
  let key, subkey;
  for (const line of blob.split('\n').filter(Boolean)) {
    if (line.startsWith('[')) {
      [ , key, subkey ] = line.match(LINE_RE)
      if (!cfg[key]) cfg[key] = {}
      if (subkey && !cfg[key][subkey]) cfg[key][subkey] = {}
      continue
    }

    const target = subkey ? cfg[key][subkey] : cfg[key]
    let [ k, v ] = line.trim().split('=').map((s) => s.trim())

    if (v === 'true' || v === 'false') v = v === 'true'
    if (v.match(/^\d+$/)) v = Number(v)

    target[k] = v
  }

  return cfg
}

async function readRepositoryMeta (path) {
  const gitFolder = resolve(path, '.git')
  if (!existsSync(gitFolder)) return null

  const headFile = join(gitFolder, 'HEAD')
  const head = await readFile(headFile, 'utf8')
  if (!head.startsWith('ref: ')) return { detached: true }

  const ref = head.slice(5).trim()
  const branch = ref.split('/').pop()
  const localRef = join(gitFolder, ref)
  const cfgFile = join(gitFolder, 'config')

  if (!existsSync(localRef)) {
    return {
      detached: false,
      upstream: null,
      revision: null,
      branch: branch,
    };
  }

  const localRevision = await readFile(localRef, 'utf8').then((ref) => ref.trim())
  const configBlob = await readFile(cfgFile, 'utf8')
  const config = parseGitConfig(configBlob)
  if (!config.branch?.[branch]?.remote || !config.remote?.[config.branch[branch].remote]) {
    return {
      detached: false,
      upstream: null,
      revision: localRevision,
      branch: branch,
    };
  }

  const { remote } = config.branch[branch]
  const upstreamUrl = config.remote[remote].url
  const repo = upstreamUrl.match(/^(?:https?:\/\/[^/]+|[^@]+@[^:]+:)(.*?)(?:\.git)?$/)[1]

  return {
    branch: branch,
    detached: false,
    revision: localRevision,
    upstream: {
      name: remote,
      url: upstreamUrl,
      repo: repo,
    },
  }
}

module.exports = { readRepositoryMeta }

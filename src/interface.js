// Interface to let the wasm binary use http/https bundled with Node

const connections = new Map()
let connectionId = 0

function createConnection (url, isPost) {
  const connId = ++connectionId
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
  connections.set(connId, { req: req, data: null, ptr: 0 })
  return connId;
}

function writeToConnection (connId, bufferPtr, length) {
  const conn = connections.get(connId)
  if (!conn) return -1

  const buffer = HEAPU8.slice(bufferPtr, bufferPtr + length)
  conn.req.write(buffer)
  return 0
}

async function readFromConnection (connId, bufferPtr, length) {
  // [Cynthia] This method reads the entire buffer and puts it in memory.
  // This may not be the most efficient way of doing it, and I kinda want
  // to refactor it to more cleanly read the connection.
  const conn = connections.get(connId)
  if (!conn) return -1

  if (!conn.data) {
    conn.data = await new Promise((resolve) => {
      conn.req.end()
      conn.req.on('response', (res) => {
        const chunks = []
        res.on('data', (chk) => chunks.push(chk))
        res.on('end', () => resolve(Buffer.concat(chunks)))
      })
    })
  }

  const read = Math.min(conn.data.length - conn.ptr, length)
  Module.writeArrayToMemory(conn.data.slice(conn.ptr, conn.ptr + read), bufferPtr)
  conn.ptr += read
  return read
}

function freeConnection (connId) {
  const conn = connections.get(connId)
  if (conn) {
    connections.delete(connId)
    // Silence any error that may arise from the destroy call
    conn.req.on('error', () => void 0)
    conn.req.destroy()
  }
}

Object.assign(Module, {
  createConnection: createConnection,
  writeToConnection: writeToConnection,
  readFromConnection: readFromConnection,
  freeConnection: freeConnection
})

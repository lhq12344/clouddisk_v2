local cjson = require("cjson")

local _M = {}
_M.__index = _M

function _M.new(conf)
    return setmetatable({
        host = conf.host,
        port = conf.port,
        service = conf.service,
        dict = ngx.shared.consul_services,
    }, _M)
end

local function consul_get(host, port, path, timeout_ms)
    local sock, err = ngx.socket.tcp()
    if not sock then
        return nil, err
    end

    sock:settimeout(timeout_ms or 1000)

    local ok, conn_err = sock:connect(host, port)
    if not ok then
        sock:close()
        return nil, conn_err
    end

    local req = "GET " .. path .. " HTTP/1.1\r\n"
        .. "Host: " .. host .. ":" .. port .. "\r\n"
        .. "Accept: application/json\r\n"
        .. "Connection: close\r\n\r\n"

    local _, send_err = sock:send(req)
    if send_err then
        sock:close()
        return nil, send_err
    end

    local status_line, status_err = sock:receive("*l")
    if not status_line then
        sock:close()
        return nil, status_err
    end

    local status = tonumber(status_line:match("HTTP/%d%.%d%s+(%d+)"))
    if not status then
        sock:close()
        return nil, "invalid Consul response status line: " .. status_line
    end

    while true do
        local line, header_err = sock:receive("*l")
        if not line then
            sock:close()
            return nil, header_err
        end
        if line == "" then
            break
        end
    end

    local chunks = {}
    while true do
        local chunk, read_err, partial = sock:receive(8192)
        if chunk then
            chunks[#chunks + 1] = chunk
        elseif partial and partial ~= "" then
            chunks[#chunks + 1] = partial
        end

        if read_err == "closed" then
            break
        elseif read_err then
            sock:close()
            return nil, read_err
        end
    end

    sock:close()
    return { status = status, body = table.concat(chunks) }
end

function _M:fetch()
    local path = "/v1/health/service/" .. self.service .. "?passing=true"
    local res, err = consul_get(self.host, self.port, path, 1000)
    if not res then
        ngx.log(ngx.ERR, "Consul request failed: ", err)
        return false, err
    end

    if res.status ~= 200 then
        ngx.log(ngx.ERR, "Consul health request failed, status: ", res.status, ", body: ", res.body)
        return false, "unexpected Consul status " .. tostring(res.status)
    end

    local ok, body = pcall(cjson.decode, res.body)
    if not ok then
        ngx.log(ngx.ERR, "JSON decode error: ", body)
        return false, body
    end

    local servers = {}
    for _, svc in ipairs(body) do
        if svc.Service and svc.Service.Address and svc.Service.Port then
            table.insert(servers, svc.Service.Address .. ":" .. svc.Service.Port)
        end
    end

    if #servers == 0 then
        ngx.log(ngx.ERR, "No passing instances returned by Consul for service: ", self.service)
        return false, "no passing instances"
    end

    local encoded = cjson.encode(servers)
    self.dict:set(self.service, encoded)
    ngx.log(ngx.INFO, "Updated service list for ", self.service, ": ", encoded)
    return true
end

return _M

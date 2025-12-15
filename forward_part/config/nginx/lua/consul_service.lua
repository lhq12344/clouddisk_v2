local http = require("resty.http")
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

function _M:fetch()
    local httpc = http.new()

    local url = "http://" .. self.host .. ":" .. self.port
        .. "/v1/health/service/" .. self.service .. "?passing=true"


    local res, err = httpc:request_uri(url, { method = "GET" })
    if not res then
        ngx.log(ngx.ERR, "HTTP request failed: ", err)
        return
    end

    local ok, body = pcall(cjson.decode, res.body)
    if not ok then
        ngx.log(ngx.ERR, "JSON decode error: ", body)
        return
    end

    local servers = {}
    for _, svc in ipairs(body) do
        table.insert(servers, svc.Service.Address .. ":" .. svc.Service.Port)
    end

    local encoded = cjson.encode(servers)
    self.dict:set(self.service, encoded)
    --ngx.log(ngx.INFO, "Updated service list: ", encoded)
end

return _M

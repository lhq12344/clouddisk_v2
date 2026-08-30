local cjson = require("cjson")
local balancer = require("ngx.balancer")  -- 注意是 ngx.balancer

local _M = {}

function _M.pick(service_name)
    local dict = ngx.shared.consul_services
    local json = dict:get(service_name)

    if not json then
        ngx.log(ngx.ERR, "No service list in shared dict for ", service_name)
        return false, "no cached service list"
    end

    local ok, servers = pcall(cjson.decode, json)
    if not ok or type(servers) ~= "table" or #servers == 0 then
        ngx.log(ngx.ERR, "Invalid servers list for ", service_name, ": ", json)
        return false, "invalid cached service list"
    end

    -- 选一个下标：这里用 request_id 做 hash，保证是整数
    local idx
    local rid = ngx.ctx.request_id 

    if rid and #servers > 1 then
        -- ngx.crc32_short 返回整数，对字符串安全
        local h = ngx.crc32_short(rid)
        idx = (h % #servers) + 1
    else
        idx = 1
    end

    local target = servers[idx]
    local host, port = target:match("(.+):(%d+)")  --注意没有做ipv6的兼容
    port = tonumber(port)
    if not host or not port then
        ngx.log(ngx.ERR, "Invalid target address for ", service_name, ": ", target)
        return false, "invalid target address"
    end

    ngx.log(ngx.INFO, "proxy to ", service_name, " => ", host, ":", port)

    local ok2, err2 = balancer.set_current_peer(host, port)
    if not ok2 then
        ngx.log(ngx.ERR, "set_current_peer failed: ", err2)
        return false, err2
    end

    return true
end

return _M

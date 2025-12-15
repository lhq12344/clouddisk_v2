#ifndef REDISPOOL
#define REDISPOOL

#include "config/config.h"
#include <hiredis/hiredis.h>
#include <memory>

class RedisPool
{
public:
    RedisPool(const RedisConfig &cfg, size_t poolSize)
        : cfg_(cfg)
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            redisContext *c = redisConnect(cfg_.host.c_str(), cfg_.port);
            if (!c || c->err)
            {
                if (c)
                {
                    std::cerr << "Redis connect error: " << c->errstr << std::endl;
                    redisFree(c);
                }
                else
                {
                    std::cerr << "Redis connect allocation error" << std::endl;
                }
                throw std::runtime_error("Redis connect failed");
            }
            pool_.push_back(c);
        }
    }

    ~RedisPool()
    {
        for (auto c : pool_)
        {
            redisFree(c);
        }
    }

    redisContext *get()
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while (pool_.empty())
        {
            cv_.wait(lk);
        }
        redisContext *c = pool_.back();
        pool_.pop_back();
        // 检查连接是否健康
        if (c->err)
        {
            std::cerr << "Redis connection err, reconnecting: " << c->errstr << std::endl;
            redisFree(c);
            c = redisConnect(cfg_.host.c_str(), cfg_.port);
            if (!c || c->err)
            {
                throw std::runtime_error("Redis reconnect failed");
            }
        }
        return c;
    }

    void put(redisContext *c)
    {
        std::unique_lock<std::mutex> lk(mtx_);
        pool_.push_back(c);
        cv_.notify_one();
    }

private:
    RedisConfig cfg_;
    std::vector<redisContext *> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif // !REDISPOOL

#ifndef EMAILSERVER_H
#define EMAILSERVER_H

#include "redispool.h"
#include "emailsend.h"
#include "config/config.h"
#include <random>
#include <chrono>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// ============ 工具：生成验证码 ============
string gen_code()
{
    static thread_local std::mt19937 rng(
        (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(0, 999999);
    int x = dist(rng);
    char buf[8];
    snprintf(buf, sizeof(buf), "%06d", x);
    return string(buf);
}

// ============ 邮件任务 ============
struct EmailTask
{
    string email;
};

// ============ 邮件服务：线程池 + Redis + Sender ============
class EmailService
{
public:
    EmailService(const AppConfig &cfg)
        : cfg_(cfg),
          redisPool_(cfg_.redis, 8),
          sender_(cfg_.smtp),
          stop_(false)
    {
        int n = cfg_.worker_threads > 0 ? cfg_.worker_threads : 8;
        for (int i = 0; i < n; ++i)
        {
            workers_.emplace_back(&EmailService::workerLoop, this, i);
        }
    }

    ~EmailService()
    {
        stop_ = true;
        cv_.notify_all();
        for (auto &t : workers_)
        {
            if (t.joinable())
                t.join();
        }
    }

    void enqueue(const EmailTask &task)
    {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            tasks_.push(task);
        }
        cv_.notify_one();
    }

private:
    void workerLoop(int idx)
    {
        while (!stop_)
        {
            EmailTask task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&] {
                    return stop_ || !tasks_.empty();
                });
                if (stop_)
                    break;
                task = tasks_.front();
                tasks_.pop();
            }

            handleTask(task);
        }
    }

    void handleTask(const EmailTask &task)
    {
        try
        {
            auto email = task.email;
            // 1. 防重复：email_dedup:{email} 60秒内只能发一次
            string dedupKey = "email_dedup:" + email;

            redisContext *c = redisPool_.get();
            redisReply *r = (redisReply *)redisCommand(
                c, "SET %s 1 NX EX %d", dedupKey.c_str(), cfg_.email.dedup_ttl_sec);
            bool canSend = false;
            if (r)
            {
                if (r->type == REDIS_REPLY_STATUS && string(r->str) == "OK")
                    canSend = true; // SET 成功，说明之前没有锁
                freeReplyObject(r);
            }
            else
            {
                std::cerr << "Redis SET NX failed for dedup key" << std::endl;
            }

            if (!canSend)
            {
                std::cout << "[EmailService] Skip duplicate email within dedup ttl: " << email << std::endl;
                redisPool_.put(c);
                return;
            }

            // 2. 生成验证码，写入 redis：email_code:{email}
            string code = gen_code();
            string codeKey = email;
            r = (redisReply *)redisCommand(
                c, "SET %s %s EX %d", codeKey.c_str(), code.c_str(), cfg_.email.code_ttl_sec);
            if (!r)
            {
               std::cerr << "Redis SET code failed" << std::endl;
                redisPool_.put(c);
                return;
            }
            freeReplyObject(r);
            redisPool_.put(c);

            // 3. 准备邮件内容（支持 text/plain 或 html）
            string subject = "您的 CloudDisk 登录验证码";
            // 简单 text 内容
            string body = "您的验证码为: " + code + "\n有效期 "
                          + std::to_string(cfg_.email.code_ttl_sec / 60) + " 分钟，请勿泄露。";

            bool ok = sender_.sendEmail(email, subject, body, false);
            if (ok)
            {
                std::cout << "[EmailService] Sent code " << code << " to " << email << std::endl;
            }
            else
            {
                std::cerr << "[EmailService] Failed to send email to " << email << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[EmailService] handleTask exception: " << e.what() << std::endl;
        }
    }

private:
    const AppConfig &cfg_;
    RedisPool redisPool_;
    EmailSender sender_;

    std::vector< std::thread> workers_;
    std::queue<EmailTask> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};

#endif // !EMAILSERVER_H

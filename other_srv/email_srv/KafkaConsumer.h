#ifndef KAFKACONSUMER_H
#define KAFKACONSUMER_H

#include "config/config.h"
#include "emailsend.h"
#include "emailServer.h"
#include <librdkafka/rdkafka.h>

static std::atomic<bool> g_running{true};


class KafkaConsumer
{
public:
    KafkaConsumer(const KafkaConfig &cfg, EmailService &svc)
        : cfg_(cfg), emailSvc_(svc)
    {
        char errstr[512];
        rd_kafka_conf_t *conf = rd_kafka_conf_new();

        // group.id
        if (rd_kafka_conf_set(conf, "group.id", cfg_.group_id.c_str(),
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            throw std::runtime_error(string("Failed to set group.id: ") + errstr);
        }

        // bootstrap.servers
        if (rd_kafka_conf_set(conf, "bootstrap.servers", cfg_.brokers.c_str(),
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            throw std::runtime_error(string("Failed to set bootstrap.servers: ") + errstr);
        }

        // 自动提交 offset
        rd_kafka_conf_set(conf, "enable.auto.commit", "true", nullptr, 0);

        // 创建 consumer
        rk_ = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
        if (!rk_)
        {
            throw std::runtime_error(string("Failed to create kafka consumer: ") + errstr);
        }

        // 订阅 topic
        rd_kafka_poll_set_consumer(rk_);

        rd_kafka_topic_partition_list_t *topics =
            rd_kafka_topic_partition_list_new(1);
        rd_kafka_topic_partition_list_add(topics, cfg_.topic.c_str(), -1);

        rd_kafka_resp_err_t err = rd_kafka_subscribe(rk_, topics);
        rd_kafka_topic_partition_list_destroy(topics);
        if (err)
        {
            throw std::runtime_error(string("Failed to subscribe: ") + rd_kafka_err2str(err));
        }

        std::cout << "[Kafka] Consumer subscribed to topic: " << cfg_.topic << std::endl;
    }

    ~KafkaConsumer()
    {
        if (rk_)
        {
            rd_kafka_consumer_close(rk_);
            rd_kafka_destroy(rk_);
            rk_ = nullptr;
        }
    }

    void loop()
    {
        while (g_running)
        {
            rd_kafka_message_t *rkmessage =
                rd_kafka_consumer_poll(rk_, 1000); // 1秒超时
            if (!rkmessage)
                continue;

            if (rkmessage->err)
            {
                if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF)
                {
                    // 正常到达分区末尾
                }
                else if (rkmessage->err == RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN)
                {
                    std::cerr << "[Kafka] All brokers down, will keep retrying..." << std::endl;
                }
                else
                {
                    std::cerr << "[Kafka] Error: " << rd_kafka_message_errstr(rkmessage) << std::endl;
                }
                rd_kafka_message_destroy(rkmessage);
                continue;
            }

            // 正常消息
            if (rkmessage->payload && rkmessage->len > 0)
            {
                string email((char *)rkmessage->payload, rkmessage->len);
                // 你目前是把 email 当纯字符串发过来的，这里兼容一下 %40 -> @
                size_t pos = email.find("%40");
                if (pos != string::npos)
                {
                    email.replace(pos, 3, "@");
                }

                std::cout << "[Kafka] Received email task: " << email <<  std::endl;
                EmailTask task{email};
                emailSvc_.enqueue(task);
            }

            rd_kafka_message_destroy(rkmessage);
        }

         std::cout << "[Kafka] loop exited" <<  std::endl;
    }

private:
    KafkaConfig cfg_;
    EmailService &emailSvc_;
    rd_kafka_t *rk_ = nullptr;
};


#endif // !KAFKACONSUMER_H
#pragma once
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <string>
#include <iostream>
#include <mutex>

class KafkaProducer : public RdKafka::DeliveryReportCb {
public:
    static KafkaProducer& instance() {
        static KafkaProducer inst;
        return inst;
    }

    bool init(const std::string& brokers) {
        std::string errstr;

        RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        RdKafka::Conf *tconf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);

        // 设置 broker 列表
        if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
            std::cerr << "Kafka conf error: " << errstr << std::endl;
            return false;
        }

        // 设置 delivery 回调
        if (conf->set("dr_cb", this, errstr) != RdKafka::Conf::CONF_OK) {
            std::cerr << "Kafka set dr_cb error: " << errstr << std::endl;
            return false;
        }

        producer_.reset(RdKafka::Producer::create(conf, errstr));
        if (!producer_) {
            std::cerr << "Kafka producer create failed: " << errstr << std::endl;
            return false;
        }

        std::cout << "✔ Kafka Producer initialized: " << brokers << std::endl;

        delete conf;
        delete tconf;
        return true;
    }

    // 异步发送消息
    bool send(const std::string& topic, const std::string& msg) {
        if (!producer_) return false;

        RdKafka::ErrorCode err = producer_->produce(
            topic,
            RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY /*复制消息*/,
            (void*)msg.data(), msg.size(),
            nullptr, 0,
            0,
            nullptr
        );

        if (err != RdKafka::ERR_NO_ERROR) {
            std::cerr << "Kafka produce error: " << RdKafka::err2str(err) << std::endl;
            return false;
        }

        // 触发回调
        producer_->poll(0);
        return true;
    }

    // delivery 回调
    void dr_cb(RdKafka::Message &msg) override {
        if (msg.err()) {
            std::cerr << " Delivery failed: " << msg.errstr() << std::endl;
        } else {
            std::cout << "✔ Delivered message to " 
                      << msg.topic_name() << " [" << msg.partition() << "]" 
                      << std::endl;
        }
    }

private:
    KafkaProducer() = default;
    std::unique_ptr<RdKafka::Producer> producer_;
};

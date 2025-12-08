#ifndef EMAILSEND_H
#define EMAILSEND_H

#include "config/config.h"
#include <curl/curl.h>

struct CurlPayload
{
    const string *data;
    size_t pos;
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    CurlPayload *pload = (CurlPayload *)userp;
    size_t room = size * nmemb;
    size_t left = pload->data->size() - pload->pos;
    if (left == 0)
        return 0;
    size_t tocopy = left < room ? left : room;
    memcpy(ptr, pload->data->data() + pload->pos, tocopy);
    pload->pos += tocopy;
    return tocopy;
}

class EmailSender
{
public:
    EmailSender(const SmtpConfig &cfg) : cfg_(cfg)
    {
    }

    // text/plain 或 text/html
    bool sendEmail(const string &to, const string &subject, const string &body, bool isHtml)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            std::cerr << "curl init failed" << std::endl;
            return false;
        }
    
        // ====== 1. 组装邮件 Payload ======
        string payload;
        payload += "From: " + cfg_.from_name + " <" + cfg_.from + ">\r\n";
        payload += "To: <" + to + ">\r\n";
        payload += "Subject: " + subject + "\r\n";
    
        if (isHtml)
            payload += "Content-Type: text/html; charset=UTF-8\r\n\r\n";
        else
            payload += "Content-Type: text/plain; charset=UTF-8\r\n\r\n";
    
        payload += body;
        payload += "\r\n";
    
        CurlPayload pl{&payload, 0};
    
        // ====== 2. 收件人列表 ======
        struct curl_slist *recipients = nullptr;
        recipients = curl_slist_append(recipients, ("<" + to + ">").c_str());
    
        // ====== 3. 设置SMTP参数 ======
        curl_easy_setopt(curl, CURLOPT_URL, cfg_.url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg_.user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg_.pass.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + cfg_.from + ">").c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    
        // 关键：SMTP AUTH LOGIN（国内 SMTP 必须）
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=LOGIN");
    
        // ====== 4. 设置 payload 回调 ======
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &pl);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
        // ====== 5. SSL/TLS（SMTP SSL端口必须） ======
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
        // ====== 6. 超时参数 ======
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    
        // ====== 7. 调试输出（可以关闭） ======
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    
        // ====== 8. 执行发送 ======
        CURLcode res = curl_easy_perform(curl);
    
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    
        if (res != CURLE_OK)
        {
            std::cerr << "curl send failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
    
        return true;
    }


private:
    SmtpConfig cfg_;
};

#endif // !EMAILSEND_H

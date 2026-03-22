package main

import (
    "encoding/json"
    "fmt"
    "os"

    "github.com/IBM/sarama"
    "github.com/google/uuid"
    "go_test/backword_part/model"
)

func main() {
    cfg := sarama.NewConfig()
    cfg.Version = sarama.V3_5_0_0
    cfg.Producer.Return.Successes = true
    producer, err := sarama.NewSyncProducer([]string{"127.0.0.1:31092"}, cfg)
    if err != nil { panic(err) }
    defer producer.Close()

    payloads := []model.FileEventPayload{
        {
            TxID:        uuid.NewString(),
            EventID:     uuid.NewString() + ":SCAN_REQUESTED",
            FileID:      15,
            Sha1:        "201a89cad2f5c4aee6e72e5ab7696be73eb2e3d71cbb0165f013f1b0225568c5",
            Size:        44039471,
            OssKey:      "files/201a89cad2f5c4aee6e72e5ab7696be73eb2e3d71cbb0165f013f1b0225568c5",
            ContentType: "application/octet-stream",
            EventType:   model.FileScanRequested,
        },
        {
            TxID:        uuid.NewString(),
            EventID:     uuid.NewString() + ":SCAN_REQUESTED",
            FileID:      14,
            Sha1:        "e55b896a2c65c3da3ea03d5ba7a4b7a36911f0a7289689921725a93194327703",
            Size:        118945608,
            OssKey:      "files/e55b896a2c65c3da3ea03d5ba7a4b7a36911f0a7289689921725a93194327703",
            ContentType: "application/vnd.debian.binary-package",
            EventType:   model.FileScanRequested,
        },
    }

    for _, payload := range payloads {
        body, err := json.Marshal(payload)
        if err != nil { panic(err) }
        _, offset, err := producer.SendMessage(&sarama.ProducerMessage{
            Topic: model.FileUploadEventTopic,
            Key:   sarama.StringEncoder(payload.Sha1),
            Value: sarama.ByteEncoder(body),
            Headers: []sarama.RecordHeader{{
                Key:   []byte("x-request-id"),
                Value: []byte(payload.EventID),
            }},
        })
        if err != nil { panic(err) }
        fmt.Printf("sent %s offset=%d\n", payload.EventID, offset)
    }

    _ = os.Remove(os.Args[0])
}

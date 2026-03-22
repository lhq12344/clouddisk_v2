大文件上传测试 mputest.go（当前版本走 PresignParts + 直传 MinIO）：
方式 A：快速生成稀疏文件（推荐，几乎瞬间）

fallocate -l 2G big.bin


方式 B：真实写入随机数据（更慢但更“真实”）

dd if=/dev/urandom of=big.bin bs=8M count=256

第一次上传（并发 4，默认会先计算文件 SHA-256）
go run ./test/mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4


模拟上传到一半就“断开”（比如传 10 个 part 就退出）
go run ./test/mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4 -stop_after 10

然后再次运行同一条命令（不带 -stop_after），它会：
	自动读取 big.bin.upload_state.json
	先 Status(upload_id)
	只补传缺失 part
	最终 CompleteMultipart

说明：
1. `PresignParts/Status/CompleteMultipart` 现在要求 `x-user-id`，脚本已自动附带 gRPC metadata。
2. 若 upload session 过期，脚本会删除旧 state 并重新 InitMultipart。
3. 若 InitMultipart 命中已有 `success/pending_scan/infected` 对象，脚本会直接打印复用状态并退出。

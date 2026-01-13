大文件上传测试mputest.go：
方式 A：快速生成稀疏文件（推荐，几乎瞬间）

fallocate -l 2G big.bin


方式 B：真实写入随机数据（更慢但更“真实”）

dd if=/dev/urandom of=big.bin bs=8M count=256

第一次上传（并发 4）
go run mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4


模拟上传到一半就“断开”（比如传 10 个 part 就退出）
go run mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4 -stop_after 10

然后再次运行同一条命令（不带 -stop_after），它会：
	自动读取 big.bin.upload_state.json
	先 Status(upload_id)
	只补传缺失 part
	最终 CompleteMultipart
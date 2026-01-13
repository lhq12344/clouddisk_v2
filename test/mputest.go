package tast

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	// TODO: 改成生成的 file_pb 包路径
	pb "go_test/backword_part/file_server/file_srv/protobuf"
)

type State struct {
	UploadID    string `json:"upload_id"`
	ObjectKey   string `json:"object_key"`
	PartSize    int64  `json:"part_size"`
	TotalParts  int32  `json:"total_parts"`
	FilePath    string `json:"file_path"`
	FileName    string `json:"file_name"`
	FileSize    int64  `json:"file_size"`
	UserID      string `json:"user_id"`
	ContentType string `json:"content_type"`
	FileHash    string `json:"file_hash"`
}

func loadState(path string) (*State, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var st State
	if err := json.Unmarshal(b, &st); err != nil {
		return nil, err
	}
	return &st, nil
}

func saveState(path string, st *State) error {
	b, _ := json.MarshalIndent(st, "", "  ")
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func main() {
	var (
		addr        = flag.String("addr", "127.0.0.1:50051", "grpc address")
		filePath    = flag.String("file", "", "path to big file")
		userID      = flag.String("user_id", "1", "user id")
		contentType = flag.String("content_type", "application/octet-stream", "content type")
		fileHash    = flag.String("file_hash", "", "optional file hash")
		parallel    = flag.Int("parallel", 4, "concurrency")
		chunkSize   = flag.Int("chunk_kb", 256, "data chunk size KB per Send(data)")
		stopAfter   = flag.Int("stop_after", 0, "simulate interruption: stop after uploading N missing parts (0=disable)")
		resume      = flag.Bool("resume", true, "resume if state file exists")
	)
	flag.Parse()

	if *filePath == "" {
		fmt.Println("missing -file")
		os.Exit(2)
	}

	fi, err := os.Stat(*filePath)
	if err != nil {
		panic(err)
	}
	if fi.IsDir() {
		panic("file is a directory")
	}
	fileName := filepath.Base(*filePath)
	fileSize := fi.Size()

	statePath := *filePath + ".upload_state.json"

	ctx := context.Background()
	conn, err := grpc.NewClient(*addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	// TODO: 改成你生成的 client 名称
	client := pb.NewFileServiceClient(conn)

	var st *State
	if *resume {
		if s, err := loadState(statePath); err == nil && s.UploadID != "" && s.FileSize == fileSize {
			st = s
			fmt.Printf("[resume] upload_id=%s object_key=%s\n", st.UploadID, st.ObjectKey)
		}
	}

	// 没有 state 就 init
	if st == nil {
		resp, err := client.InitMultipart(ctx, &pb.InitReq{
			UserId:      *userID,
			FileName:    fileName,
			FileSize:    fileSize,
			FileHash:    *fileHash,
			ContentType: *contentType,
		})
		if err != nil {
			panic(err)
		}
		st = &State{
			UploadID:    resp.UploadId,
			ObjectKey:   resp.ObjectKey,
			PartSize:    resp.PartSize,
			TotalParts:  resp.TotalParts,
			FilePath:    *filePath,
			FileName:    fileName,
			FileSize:    fileSize,
			UserID:      *userID,
			ContentType: *contentType,
			FileHash:    *fileHash,
		}
		if err := saveState(statePath, st); err != nil {
			panic(err)
		}
		fmt.Printf("[init] upload_id=%s part_size=%d total_parts=%d object_key=%s\n",
			st.UploadID, st.PartSize, st.TotalParts, st.ObjectKey)
	}

	// Status：拿到已上传分片
	statusResp, err := client.Status(ctx, &pb.StatusReq{UploadId: st.UploadID})
	if err != nil {
		panic(err)
	}
	uploadedSet := make(map[int32]bool, len(statusResp.UploadedParts))
	for _, pn := range statusResp.UploadedParts {
		uploadedSet[pn] = true
	}

	// 生成缺失分片列表
	missing := make([]int32, 0)
	for i := int32(1); i <= statusResp.TotalParts; i++ {
		if !uploadedSet[i] {
			missing = append(missing, i)
		}
	}
	fmt.Printf("[status] total=%d uploaded=%d missing=%d\n", statusResp.TotalParts, len(statusResp.UploadedParts), len(missing))

	if len(missing) == 0 {
		fmt.Println("[info] no missing parts, try complete directly")
	} else {
		// 并发上传缺失分片
		partCh := make(chan int32, len(missing))
		for _, pn := range missing {
			partCh <- pn
		}
		close(partCh)

		var (
			wg        sync.WaitGroup
			mu        sync.Mutex
			doneCount = 0
		)

		uploadOne := func(pn int32) error {
			// 计算该 part 的 offset/size
			offset := int64(pn-1) * st.PartSize
			ps := st.PartSize
			if offset+ps > st.FileSize {
				ps = st.FileSize - offset
			}
			if ps <= 0 {
				return fmt.Errorf("invalid part size pn=%d", pn)
			}

			f, err := os.Open(st.FilePath)
			if err != nil {
				return err
			}
			defer f.Close()

			// 每个 part 用一个独立的 client-stream
			stream, err := client.UploadPart(ctx)
			if err != nil {
				return err
			}

			// 1) 先发 meta（第一条必须是 meta）
			if err := stream.Send(&pb.UploadPartReq{
				Payload: &pb.UploadPartReq_Meta{
					Meta: &pb.UploadPartMeta{
						UploadId:   st.UploadID,
						PartNumber: pn,
						PartSize:   ps,
						ChunkHash:  "", // 可选
					},
				},
			}); err != nil {
				return err
			}

			// 2) 再发 data（多条）
			buf := make([]byte, (*chunkSize)*1024)
			reader := io.NewSectionReader(f, offset, ps)
			for {
				n, rerr := reader.Read(buf)
				if n > 0 {
					if err := stream.Send(&pb.UploadPartReq{
						Payload: &pb.UploadPartReq_Data{Data: buf[:n]},
					}); err != nil {
						return err
					}
				}
				if rerr == io.EOF {
					break
				}
				if rerr != nil {
					return rerr
				}
			}

			// 3) CloseAndRecv 拿到 etag
			r, err := stream.CloseAndRecv()
			if err != nil {
				return err
			}
			fmt.Printf("[part ok] pn=%d etag=%s\n", pn, r.Etag)
			return nil
		}

		worker := func() {
			defer wg.Done()
			for pn := range partCh {
				// 简单重试（网络不稳时很有用）
				var lastErr error
				for attempt := 1; attempt <= 5; attempt++ {
					lastErr = uploadOne(pn)
					if lastErr == nil {
						break
					}
					time.Sleep(time.Duration(attempt) * 500 * time.Millisecond)
				}
				if lastErr != nil {
					fmt.Printf("[part fail] pn=%d err=%v\n", pn, lastErr)
					os.Exit(1)
				}

				mu.Lock()
				doneCount++
				// 模拟中断：上传到一定数量后直接退出（用来验证断点续传）
				if *stopAfter > 0 && doneCount >= *stopAfter {
					fmt.Printf("[simulate] stop_after=%d reached, exit now (re-run to resume)\n", *stopAfter)
					os.Exit(0)
				}
				mu.Unlock()
			}
		}

		nw := *parallel
		if nw <= 0 {
			nw = 1
		}
		wg.Add(nw)
		for i := 0; i < nw; i++ {
			go worker()
		}
		wg.Wait()
		fmt.Println("[upload] all missing parts uploaded")
	}

	// Complete
	comp, err := client.CompleteMultipart(ctx, &pb.CompleteReq{UploadId: st.UploadID})
	if err != nil {
		panic(err)
	}
	fmt.Printf("[complete] object_key=%s\n", comp.ObjectKey)

	// 更新 state
	st.ObjectKey = comp.ObjectKey
	_ = saveState(statePath, st)

	// 可选：打印最终状态
	finalStatus, _ := client.Status(ctx, &pb.StatusReq{UploadId: st.UploadID})
	sort.Slice(finalStatus.UploadedParts, func(i, j int) bool { return finalStatus.UploadedParts[i] < finalStatus.UploadedParts[j] })
	fmt.Printf("[final status] total=%d uploaded=%d\n", finalStatus.TotalParts, len(finalStatus.UploadedParts))
}

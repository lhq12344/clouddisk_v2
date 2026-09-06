package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	storagecontrolpb "go_test/clouddisk_v2/storage_control/protobuf"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
)

const defaultPartSize = 10 * 1024 * 1024

type State struct {
	StorageUploadID string `json:"storage_upload_id"`
	ObjectKey       string `json:"object_key"`
	PartSize        int64  `json:"part_size"`
	TotalParts      int32  `json:"total_parts"`
	FilePath        string `json:"file_path"`
	FileName        string `json:"file_name"`
	FileSize        int64  `json:"file_size"`
	UserID          string `json:"user_id"`
	ContentType     string `json:"content_type"`
	FileHash        string `json:"file_hash"`
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
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func withRequestID(ctx context.Context, userID string) context.Context {
	return metadata.AppendToOutgoingContext(
		ctx,
		"x-user-id", userID,
		"x-request-id", fmt.Sprintf("mputest-%d", time.Now().UnixNano()),
	)
}

func calculateFileSHA256(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func totalParts(fileSize, partSize int64) int32 {
	if fileSize <= 0 || partSize <= 0 {
		return 0
	}
	return int32((fileSize + partSize - 1) / partSize)
}

func chunkPartNumbers(items []int32, size int) [][]int32 {
	if size <= 0 {
		size = 1
	}
	var batches [][]int32
	for i := 0; i < len(items); i += size {
		end := i + size
		if end > len(items) {
			end = len(items)
		}
		batches = append(batches, items[i:end])
	}
	return batches
}

func uploadPresignedPart(ctx context.Context, url, filePath string, offset, size int64) (string, error) {
	f, err := os.Open(filePath)
	if err != nil {
		return "", err
	}
	defer f.Close()

	req, err := http.NewRequestWithContext(ctx, http.MethodPut, url, io.NewSectionReader(f, offset, size))
	if err != nil {
		return "", err
	}
	req.ContentLength = size

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return "", fmt.Errorf("put part failed: status=%s body=%s", resp.Status, string(body))
	}
	return resp.Header.Get("ETag"), nil
}

func main() {
	var (
		addr        = flag.String("addr", "127.0.0.1:50054", "storage_control grpc address")
		filePath    = flag.String("file", "", "path to big file")
		userID      = flag.String("user_id", "1", "user id for request metadata only")
		contentType = flag.String("content_type", "application/octet-stream", "content type")
		fileHash    = flag.String("file_hash", "", "optional file hash; defaults to SHA-256 of file content")
		parallel    = flag.Int("parallel", 4, "parallel part uploads")
		stopAfter   = flag.Int("stop_after", 0, "upload N missing parts and exit without complete")
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

	if *fileHash == "" {
		fmt.Println("[hash] calculating SHA-256...")
		*fileHash, err = calculateFileSHA256(*filePath)
		if err != nil {
			panic(err)
		}
	}

	fileName := filepath.Base(*filePath)
	fileSize := fi.Size()
	statePath := *filePath + ".upload_state.json"
	objectKey := "files/" + *fileHash
	baseCtx := context.Background()

	conn, err := grpc.NewClient(*addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	client := storagecontrolpb.NewStorageControlClient(conn)

	var st *State
	if *resume {
		if s, err := loadState(statePath); err == nil &&
			s.StorageUploadID != "" &&
			s.FileSize == fileSize &&
			s.FileHash == *fileHash &&
			s.UserID == *userID {
			st = s
			fmt.Printf("[resume] storage_upload_id=%s object_key=%s\n", st.StorageUploadID, st.ObjectKey)
		}
	}

	initMultipart := func() *State {
		resp, err := client.InitiateMultipart(withRequestID(baseCtx, *userID), &storagecontrolpb.InitiateMultipartReq{
			ObjectKey:   objectKey,
			ContentType: *contentType,
			Metadata: map[string]string{
				"x-file-name": fileName,
				"x-file-hash": *fileHash,
				"x-user-id":   *userID,
			},
		})
		if err != nil {
			panic(err)
		}

		next := &State{
			StorageUploadID: resp.StorageUploadId,
			ObjectKey:       objectKey,
			PartSize:        defaultPartSize,
			TotalParts:      totalParts(fileSize, defaultPartSize),
			FilePath:        *filePath,
			FileName:        fileName,
			FileSize:        fileSize,
			UserID:          *userID,
			ContentType:     *contentType,
			FileHash:        *fileHash,
		}
		if err := saveState(statePath, next); err != nil {
			panic(err)
		}
		fmt.Printf("[init] storage_upload_id=%s part_size=%d total_parts=%d object_key=%s\n",
			next.StorageUploadID, next.PartSize, next.TotalParts, next.ObjectKey)
		return next
	}

	if st == nil {
		st = initMultipart()
	}

	statusResp, err := client.ListParts(withRequestID(baseCtx, st.UserID), &storagecontrolpb.ListPartsReq{
		ObjectKey:       st.ObjectKey,
		StorageUploadId: st.StorageUploadID,
	})
	if err != nil {
		_ = os.Remove(statePath)
		fmt.Printf("[resume] list parts failed, re-init upload session: %v\n", err)
		st = initMultipart()
		statusResp, err = client.ListParts(withRequestID(baseCtx, st.UserID), &storagecontrolpb.ListPartsReq{
			ObjectKey:       st.ObjectKey,
			StorageUploadId: st.StorageUploadID,
		})
		if err != nil {
			panic(err)
		}
	}

	uploadedSet := make(map[int32]bool, len(statusResp.Parts))
	for _, part := range statusResp.Parts {
		uploadedSet[part.PartNumber] = true
	}

	missing := make([]int32, 0)
	for i := int32(1); i <= st.TotalParts; i++ {
		if !uploadedSet[i] {
			missing = append(missing, i)
		}
	}
	fmt.Printf("[status] total=%d uploaded=%d missing=%d\n", st.TotalParts, len(statusResp.Parts), len(missing))

	partialRun := *stopAfter > 0
	if partialRun && len(missing) > *stopAfter {
		missing = missing[:*stopAfter]
	}

	for _, batch := range chunkPartNumbers(missing, *parallel) {
		var (
			wg    sync.WaitGroup
			mu    sync.Mutex
			first error
		)
		for _, pn := range batch {
			pn := pn
			wg.Add(1)
			go func() {
				defer wg.Done()
				presigned, err := client.PresignPart(withRequestID(baseCtx, st.UserID), &storagecontrolpb.PresignPartReq{
					ObjectKey:       st.ObjectKey,
					StorageUploadId: st.StorageUploadID,
					PartNumber:      pn,
					ExpiresSeconds:  int64((15 * time.Minute).Seconds()),
				})
				if err != nil {
					mu.Lock()
					if first == nil {
						first = fmt.Errorf("presign part %d: %w", pn, err)
					}
					mu.Unlock()
					return
				}
				offset := int64(pn-1) * st.PartSize
				partSize := st.PartSize
				if offset+partSize > st.FileSize {
					partSize = st.FileSize - offset
				}
				etag, err := uploadPresignedPart(baseCtx, presigned.Url, st.FilePath, offset, partSize)
				mu.Lock()
				defer mu.Unlock()
				if err != nil && first == nil {
					first = fmt.Errorf("upload part %d: %w", pn, err)
					return
				}
				fmt.Printf("[part ok] pn=%d etag=%s\n", pn, etag)
			}()
		}
		wg.Wait()
		if first != nil {
			panic(first)
		}
	}

	if partialRun {
		fmt.Printf("[simulate] stop_after=%d reached, exit now and rerun to resume\n", *stopAfter)
		return
	}

	latest, err := client.ListParts(withRequestID(baseCtx, st.UserID), &storagecontrolpb.ListPartsReq{
		ObjectKey:       st.ObjectKey,
		StorageUploadId: st.StorageUploadID,
	})
	if err != nil {
		panic(err)
	}
	sort.Slice(latest.Parts, func(i, j int) bool { return latest.Parts[i].PartNumber < latest.Parts[j].PartNumber })
	complete, err := client.CompleteMultipart(withRequestID(baseCtx, st.UserID), &storagecontrolpb.CompleteMultipartReq{
		ObjectKey:       st.ObjectKey,
		StorageUploadId: st.StorageUploadID,
		ContentType:     st.ContentType,
		Parts:           latest.Parts,
	})
	if err != nil {
		panic(err)
	}

	fmt.Printf("[complete] object_key=%s etag=%s\n", complete.ObjectKey, complete.Etag)
	_ = os.Remove(statePath)
}

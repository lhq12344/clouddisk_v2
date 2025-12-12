package localfile

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
)

var BasePath = "/base/path/for/storage" // 定义文件存储的根目录
type LoadFile struct {
}

// FileExists 工具函数：检查文件是否存在
func (f *LoadFile) FileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func (f *LoadFile) PathForSha1(hash string) string {
	dir1 := hash[0:2]
	dir2 := hash[2:4]
	dirPath := filepath.Join(BasePath, dir1, dir2)
	// 组合完整文件路径，使用哈希字符串作为文件名
	Path := filepath.Join(dirPath, hash)
	return Path
}

// SaveFile 将数据保存为文件，文件名为 data 的 SHA1 哈希。返回保存的文件路径。
func (f *LoadFile) SaveFile(hash string, data []byte) (string, error) {
	// 2. 构建两级目录路径：hash[0:2] 作为一级目录，hash[2:4] 作为二级目录
	if len(hash) < 4 {
		return "", fmt.Errorf("hash too short: %s", hash)
	}
	dir1 := hash[0:2]
	dir2 := hash[2:4]
	dirPath := filepath.Join(BasePath, dir1, dir2)
	// 3. 确保目标目录存在，如不存在则递归创建
	if err := os.MkdirAll(dirPath, 0755); err != nil {
		return "", fmt.Errorf("create dir failed: %w", err)
	}
	// 4. 组合完整文件路径，使用哈希字符串作为文件名
	filePath := filepath.Join(dirPath, hash)
	// 5. 将文件数据写入磁盘
	if err := ioutil.WriteFile(filePath, data, 0644); err != nil {
		return "", fmt.Errorf("write file failed: %w", err)
	}
	return filePath, nil
}

// FindFile 根据 SHA1 哈希查找文件。
// 若文件存在，返回完整路径和 true；否则返回空字符串和 false。
func (f *LoadFile) FindFile(hash string) (string, bool) {
	if len(hash) < 4 {
		return "", false // 无效的哈希字符串
	}
	// 根据哈希构造文件路径（两级目录 + 文件名）
	dir1 := hash[0:2]
	dir2 := hash[2:4]
	filePath := filepath.Join(BasePath, dir1, dir2, hash)
	// 查询文件状态
	info, err := os.Stat(filePath)
	if err != nil {
		// 文件不存在或发生错误
		return "", false
	}
	if info.IsDir() {
		return "", false // 防御性检查: 路径不应为目录
	}
	return filePath, true
}

// DeleteFile 根据 SHA1 哈希删除文件。若删除成功返回 nil，否则返回错误。
func (f *LoadFile) DeleteFile(hash string) error {
	if len(hash) < 4 {
		return fmt.Errorf("invalid hash: %s", hash)
	}
	// 构造文件路径
	filePath := filepath.Join(BasePath, hash[0:2], hash[2:4], hash)
	// 删除文件本身
	if err := os.Remove(filePath); err != nil {
		if os.IsNotExist(err) {
			return nil // 文件本就不存在
		}
		return fmt.Errorf("remove file failed: %w", err)
	}
	// 尝试清理空目录（第二级和第一级），忽略错误以防止并发删除导致的问题
	_ = os.Remove(filepath.Dir(filePath))               // 删除二级目录（如已空）
	_ = os.Remove(filepath.Dir(filepath.Dir(filePath))) // 删除一级目录（如已空）
	return nil
}

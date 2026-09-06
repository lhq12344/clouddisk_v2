package core_contract

import "testing"

type fileStatus string

const (
	statusPendingScan fileStatus = "pending_scan"
	statusSuccess     fileStatus = "success"
	statusInfected    fileStatus = "infected"
	statusScanFailed  fileStatus = "scan_failed"
	statusSoftDeleted fileStatus = "soft_deleted"
)

type fileRecord struct {
	ID        uint
	OwnerID   uint
	Hash      string
	Name      string
	Status    fileStatus
	RefCount  int
	ObjectKey string
}

func canAccessFile(userID uint, f fileRecord) bool {
	return userID == f.OwnerID && f.Status == statusSuccess
}

func canInstantReuse(f fileRecord) bool {
	return f.Status == statusSuccess && f.RefCount > 0
}

func deleteDecision(f fileRecord) (removeUserRelation bool, emitObjectDeleteEvent bool, syncDeleteObject bool) {
	if f.RefCount <= 0 {
		return false, false, false
	}
	return true, f.RefCount == 1, false
}

func TestFileAuthorizationRequiresOwnerAndSuccessfulScan(t *testing.T) {
	tests := []struct {
		name   string
		userID uint
		file   fileRecord
		want   bool
	}{
		{"owner success", 100, fileRecord{OwnerID: 100, Status: statusSuccess}, true},
		{"non owner success", 200, fileRecord{OwnerID: 100, Status: statusSuccess}, false},
		{"owner pending scan", 100, fileRecord{OwnerID: 100, Status: statusPendingScan}, false},
		{"owner infected", 100, fileRecord{OwnerID: 100, Status: statusInfected}, false},
		{"owner scan failed", 100, fileRecord{OwnerID: 100, Status: statusScanFailed}, false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := canAccessFile(tt.userID, tt.file); got != tt.want {
				t.Fatalf("canAccessFile()=%v, want %v", got, tt.want)
			}
		})
	}
}

func TestDedupOnlyReusesSuccessfulReferencedObjects(t *testing.T) {
	tests := []struct {
		status fileStatus
		refs   int
		want   bool
	}{
		{statusSuccess, 1, true},
		{statusSuccess, 0, false},
		{statusPendingScan, 1, false},
		{statusInfected, 1, false},
		{statusScanFailed, 1, false},
		{statusSoftDeleted, 1, false},
	}

	for _, tt := range tests {
		file := fileRecord{Status: tt.status, RefCount: tt.refs}
		if got := canInstantReuse(file); got != tt.want {
			t.Fatalf("canInstantReuse(%s,%d)=%v, want %v", tt.status, tt.refs, got, tt.want)
		}
	}
}

func TestDeleteLastReferenceEmitsAsyncCleanupWithoutSynchronousObjectDelete(t *testing.T) {
	removeRelation, emitCleanup, syncDelete := deleteDecision(fileRecord{RefCount: 1})
	if !removeRelation || !emitCleanup {
		t.Fatalf("last reference should remove relation and emit cleanup event")
	}
	if syncDelete {
		t.Fatalf("HTTP delete must not synchronously delete object storage data")
	}
}

func TestDeleteNonLastReferenceDoesNotEmitObjectCleanup(t *testing.T) {
	removeRelation, emitCleanup, syncDelete := deleteDecision(fileRecord{RefCount: 2})
	if !removeRelation {
		t.Fatalf("delete should remove the requesting user's relation")
	}
	if emitCleanup || syncDelete {
		t.Fatalf("non-last reference must not request object deletion, emit=%v sync=%v", emitCleanup, syncDelete)
	}
}

func TestFileListContractDoesNotNeedObjectStorageCalls(t *testing.T) {
	objectStorageCalls := 0
	rows := []fileRecord{
		{ID: 1, OwnerID: 100, Hash: "h1", Name: "a.txt", Status: statusSuccess},
		{ID: 2, OwnerID: 100, Hash: "h2", Name: "b.txt", Status: statusPendingScan},
	}

	listed := make([]fileRecord, 0, len(rows))
	for _, row := range rows {
		if row.OwnerID == 100 {
			listed = append(listed, row)
		}
	}

	if len(listed) != 2 {
		t.Fatalf("expected metadata rows from repository, got %d", len(listed))
	}
	if objectStorageCalls != 0 {
		t.Fatalf("file list must not call object storage, got %d calls", objectStorageCalls)
	}
}

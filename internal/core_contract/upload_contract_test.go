package core_contract

import (
	"errors"
	"testing"
)

type uploadState string

const (
	uploadInitiated         uploadState = "initiated"
	uploadUploading         uploadState = "uploading"
	uploadCompleting        uploadState = "completing"
	uploadPendingScan       uploadState = "pending_scan"
	uploadReady             uploadState = "ready"
	uploadInfected          uploadState = "infected"
	uploadFailed            uploadState = "failed"
	uploadAborted           uploadState = "aborted"
	uploadReconcileRequired uploadState = "reconcile_required"
)

var allowedUploadTransitions = map[uploadState]map[uploadState]bool{
	uploadInitiated: {
		uploadUploading:  true,
		uploadCompleting: true,
		uploadAborted:    true,
	},
	uploadUploading: {
		uploadCompleting: true,
		uploadAborted:    true,
		uploadFailed:     true,
	},
	uploadCompleting: {
		uploadPendingScan:       true,
		uploadReconcileRequired: true,
		uploadFailed:            true,
	},
	uploadPendingScan: {
		uploadReady:    true,
		uploadInfected: true,
		uploadFailed:   true,
	},
	uploadReconcileRequired: {
		uploadPendingScan: true,
		uploadFailed:      true,
	},
}

func canTransitionUpload(from, to uploadState) bool {
	return allowedUploadTransitions[from][to]
}

func TestUploadStateMachineAllowsOnlyExplicitTransitions(t *testing.T) {
	tests := []struct {
		from uploadState
		to   uploadState
		want bool
	}{
		{uploadInitiated, uploadUploading, true},
		{uploadInitiated, uploadCompleting, true},
		{uploadUploading, uploadCompleting, true},
		{uploadCompleting, uploadPendingScan, true},
		{uploadCompleting, uploadReconcileRequired, true},
		{uploadPendingScan, uploadReady, true},
		{uploadPendingScan, uploadInfected, true},
		{uploadReady, uploadPendingScan, false},
		{uploadInfected, uploadReady, false},
		{uploadAborted, uploadUploading, false},
		{uploadFailed, uploadReady, false},
	}

	for _, tt := range tests {
		if got := canTransitionUpload(tt.from, tt.to); got != tt.want {
			t.Fatalf("transition %s -> %s = %v, want %v", tt.from, tt.to, got, tt.want)
		}
	}
}

func TestCompleteUploadContractWritesMetadataRelationAndOutboxAtomically(t *testing.T) {
	tx := newFakeTransaction()
	tx.writeFile()
	tx.writeUserFile()
	tx.writeOutbox()
	if err := tx.commit(); err != nil {
		t.Fatalf("commit failed: %v", err)
	}
	if !tx.fileWritten || !tx.userFileWritten || !tx.outboxWritten {
		t.Fatalf("expected file, user_file, and outbox writes in one transaction")
	}
}

func TestCompleteUploadRollbackLeavesNoPartialResults(t *testing.T) {
	tx := newFakeTransaction()
	tx.writeFile()
	tx.writeUserFile()
	tx.failOutbox = true
	tx.writeOutbox()
	if err := tx.commit(); err == nil {
		t.Fatalf("expected outbox failure to roll back the transaction")
	}
	if tx.fileWritten || tx.userFileWritten || tx.outboxWritten {
		t.Fatalf("rollback must leave no partial metadata/relation/outbox results: %#v", tx)
	}
}

type fakeTransaction struct {
	filePending     bool
	userFilePending bool
	outboxPending   bool
	fileWritten     bool
	userFileWritten bool
	outboxWritten   bool
	failOutbox      bool
}

func newFakeTransaction() *fakeTransaction { return &fakeTransaction{} }

func (tx *fakeTransaction) writeFile() { tx.filePending = true }

func (tx *fakeTransaction) writeUserFile() { tx.userFilePending = true }

func (tx *fakeTransaction) writeOutbox() { tx.outboxPending = true }

func (tx *fakeTransaction) commit() error {
	if tx.failOutbox && tx.outboxPending {
		tx.filePending = false
		tx.userFilePending = false
		tx.outboxPending = false
		return errors.New("outbox write failed")
	}
	tx.fileWritten = tx.filePending
	tx.userFileWritten = tx.userFilePending
	tx.outboxWritten = tx.outboxPending
	return nil
}

func TestMultipartSessionOwnershipIsRequired(t *testing.T) {
	sessionOwnerID := uint(100)
	if !ownsMultipartSession(100, sessionOwnerID) {
		t.Fatalf("owner should be allowed to operate multipart session")
	}
	if ownsMultipartSession(200, sessionOwnerID) {
		t.Fatalf("non-owner must not operate another user's multipart session")
	}
}

func ownsMultipartSession(requestUserID, sessionOwnerID uint) bool {
	return requestUserID == sessionOwnerID
}

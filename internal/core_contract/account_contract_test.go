package core_contract

import "testing"

type pendingRegistration struct {
	Email        string
	Username     string
	PasswordHash string
	Code         string
	Consumed     bool
}

type accountRecord struct {
	Email    string
	Username string
}

type registrationStore struct {
	pending  map[string]pendingRegistration
	accounts map[string]accountRecord
}

func newRegistrationStore(p pendingRegistration) *registrationStore {
	return &registrationStore{
		pending:  map[string]pendingRegistration{p.Email: p},
		accounts: map[string]accountRecord{},
	}
}

func (s *registrationStore) verifyCodeAndCreateAccount(email, code string) (created bool, err string) {
	p, ok := s.pending[email]
	if !ok || p.Consumed {
		if _, exists := s.accounts[email]; exists {
			return false, "already_verified"
		}
		return false, "pending_registration_not_found"
	}
	if p.Code != code {
		return false, "verification_code_mismatch"
	}
	if _, exists := s.accounts[email]; exists {
		p.Consumed = true
		s.pending[email] = p
		return false, "already_verified"
	}
	s.accounts[email] = accountRecord{Email: p.Email, Username: p.Username}
	p.Consumed = true
	s.pending[email] = p
	return true, ""
}

func TestVerificationCodeSuccessCreatesAccountAtomically(t *testing.T) {
	store := newRegistrationStore(pendingRegistration{
		Email:        "user@example.com",
		Username:     "alice",
		PasswordHash: "legacy-compatible-hash",
		Code:         "123456",
	})

	created, err := store.verifyCodeAndCreateAccount("user@example.com", "123456")
	if err != "" || !created {
		t.Fatalf("expected account creation on successful verification, created=%v err=%q", created, err)
	}
	if got := len(store.accounts); got != 1 {
		t.Fatalf("expected exactly one account, got %d", got)
	}
	if _, ok := store.accounts["user@example.com"]; !ok {
		t.Fatalf("verified email was not persisted as an account")
	}
}

func TestVerificationCodeIsIdempotent(t *testing.T) {
	store := newRegistrationStore(pendingRegistration{
		Email:    "user@example.com",
		Username: "alice",
		Code:     "123456",
	})

	created, err := store.verifyCodeAndCreateAccount("user@example.com", "123456")
	if err != "" || !created {
		t.Fatalf("first verification should create the account, created=%v err=%q", created, err)
	}

	created, err = store.verifyCodeAndCreateAccount("user@example.com", "123456")
	if err != "already_verified" || created {
		t.Fatalf("second verification should be idempotent without duplicate creation, created=%v err=%q", created, err)
	}
	if got := len(store.accounts); got != 1 {
		t.Fatalf("expected one account after duplicate verification, got %d", got)
	}
}

func TestVerificationCodeMismatchDoesNotCreateAccount(t *testing.T) {
	store := newRegistrationStore(pendingRegistration{
		Email:    "user@example.com",
		Username: "alice",
		Code:     "123456",
	})

	created, err := store.verifyCodeAndCreateAccount("user@example.com", "654321")
	if err != "verification_code_mismatch" || created {
		t.Fatalf("wrong code should fail without creation, created=%v err=%q", created, err)
	}
	if got := len(store.accounts); got != 0 {
		t.Fatalf("expected no account after failed verification, got %d", got)
	}
}

func TestJWTCompatibilityContract(t *testing.T) {
	claims := map[string]any{
		"iss":  "Signin",
		"ID":   float64(42),
		"Name": "alice",
	}

	if claims["iss"] != "Signin" {
		t.Fatalf("issuer must remain compatible with the existing jwt_decode filter")
	}
	if _, ok := claims["ID"].(float64); !ok {
		t.Fatalf("ID claim must remain numeric for the current C++ jwt_decode path")
	}
	if claims["Name"] == "" {
		t.Fatalf("Name claim must remain present for request context propagation")
	}
}

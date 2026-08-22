package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"

	"github.com/mirkobrombin/go-foundation/v2/core/secrets"
)

type failingStore struct{}

func (failingStore) Set(string, []byte) error   { return errors.New("transport failed") }
func (failingStore) Get(string) ([]byte, error) { return nil, errors.New("transport failed") }
func (failingStore) Delete(string) error        { return errors.New("transport failed") }

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func main() {
	memory := secrets.NewMemoryStore()
	input := []byte("value")
	must(memory.Set("token", input))
	input[0] = 'X'
	first, err := memory.Get("token")
	must(err)
	first[0] = 'Y'
	second, err := memory.Get("token")
	must(err)
	if string(second) != "value" {
		panic("memory did not copy values")
	}
	fmt.Println("memory copies")

	must(memory.Delete("token"))
	if _, err := memory.Get("token"); !errors.Is(err, secrets.ErrNotFound) {
		panic("memory delete did not remove value")
	}
	fmt.Println("memory delete")

	environment := secrets.NewEnvStore()
	value, err := environment.Get("FOUNDATION_SECRET_TEST")
	must(err)
	if string(value) != "environment-value" ||
		!errors.Is(environment.Set("FOUNDATION_SECRET_TEST", []byte("x")), secrets.ErrReadOnly) {
		panic("environment contract failed")
	}
	fmt.Println("environment read only")

	prefixMemory := secrets.NewMemoryStore()
	prefixed := secrets.NewPrefixStore(prefixMemory, "app/")
	must(prefixed.Set("token", []byte("namespaced")))
	value, err = prefixMemory.Get("app/token")
	must(err)
	if string(value) != "namespaced" {
		panic("prefix contract failed")
	}
	fmt.Println("prefix namespace")

	fallbackMemory := secrets.NewMemoryStore()
	fallback := secrets.NewFallbackStore(secrets.NewEnvStore(), fallbackMemory)
	must(fallback.Set("FOUNDATION_SECRET_FALLBACK", []byte("secondary")))
	value, err = fallback.Get("FOUNDATION_SECRET_FALLBACK")
	must(err)
	if string(value) != "secondary" {
		panic("fallback contract failed")
	}
	fmt.Println("fallback expected conditions")

	preserving := secrets.NewFallbackStore(failingStore{}, fallbackMemory)
	if _, err := preserving.Get("FOUNDATION_SECRET_FALLBACK"); err == nil ||
		errors.Is(err, secrets.ErrNotFound) {
		panic("fallback hid a primary failure")
	}
	fmt.Println("fallback preserves failures")

	cipherMemory := secrets.NewMemoryStore()
	cipher, err := secrets.NewCipherStore(
		cipherMemory,
		[]byte("0123456789abcdef0123456789abcdef"),
	)
	must(err)
	must(cipher.Set("one", []byte("first")))
	value, err = cipher.Get("one")
	must(err)
	if string(value) != "first" {
		panic("cipher round trip failed")
	}
	raw, err := cipherMemory.Get("one")
	must(err)
	if bytes.Equal(raw, []byte("first")) {
		panic("cipher stored plaintext")
	}
	fmt.Println("cipher round trip")

	must(cipher.Set("two", []byte("second")))
	raw, err = cipherMemory.Get("two")
	must(err)
	must(cipherMemory.Set("one", raw))
	if _, err := cipher.Get("one"); err == nil {
		panic("ciphertext was not bound to its key")
	}
	fmt.Println("cipher key binding")

	values := make(map[string]string)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-Vault-Token") != "token" || r.URL.Path != "/v1/secret/data/app/db" {
			http.Error(w, "invalid request", http.StatusBadRequest)
			return
		}
		switch r.Method {
		case http.MethodPut:
			var body struct {
				Data map[string]string `json:"data"`
			}
			must(json.NewDecoder(r.Body).Decode(&body))
			values["app/db"] = body.Data["value"]
			w.WriteHeader(http.StatusNoContent)
		case http.MethodGet:
			must(json.NewEncoder(w).Encode(map[string]any{
				"request_id": "fixture",
				"data": map[string]any{
					"data": map[string]string{
						"value":   values["app/db"],
						"version": "current",
					},
					"metadata": map[string]any{"version": 1},
				},
			}))
		case http.MethodDelete:
			delete(values, "app/db")
			w.WriteHeader(http.StatusNoContent)
		}
	}))
	defer server.Close()

	vault := secrets.NewVaultStore(
		secrets.WithVaultAddress(server.URL),
		secrets.WithVaultToken("token"),
	)
	must(vault.Set("app/db", []byte("password")))
	if values["app/db"] != base64.StdEncoding.EncodeToString([]byte("password")) {
		panic("vault wire encoding failed")
	}
	value, err = vault.Get("app/db")
	must(err)
	if string(value) != "password" {
		panic("vault round trip failed")
	}
	must(vault.Delete("app/db"))
	fmt.Println("vault kv2 wire")

	missingServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
	}))
	defer missingServer.Close()
	missingVault := secrets.NewVaultStore(
		secrets.WithVaultAddress(missingServer.URL),
		secrets.WithVaultToken("token"),
	)
	if _, err := missingVault.Get("missing"); !errors.Is(err, secrets.ErrNotFound) {
		panic("vault missing contract failed")
	}
	fmt.Println("vault missing")

	invalidVault := secrets.NewVaultStore(
		secrets.WithVaultAddress("https://vault.example.com"),
		secrets.WithVaultToken("token"),
	)
	if err := invalidVault.Set("app/../db", []byte("value"));
		!errors.Is(err, secrets.ErrInvalidKey) {
		panic("vault invalid key was accepted")
	}
	fmt.Println("vault invalid key")

	insecureVault := secrets.NewVaultStore(
		secrets.WithVaultAddress("http://vault.example.com"),
		secrets.WithVaultToken("token"),
	)
	if err := insecureVault.Set("key", []byte("value"));
		!errors.Is(err, secrets.ErrInsecureVaultAddress) {
		panic("insecure vault address was accepted")
	}
	fmt.Println("insecure vault rejected")
	fmt.Println("secrets compatibility ok")
}

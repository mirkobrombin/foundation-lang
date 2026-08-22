package main

import (
	"errors"
	"fmt"
	"os"
	"strings"

	"github.com/mirkobrombin/go-foundation/v2/core/auth"
)

func main() {
	secret := []byte("0123456789abcdef0123456789abcdef")
	subject := "alice<&>\u2028end"
	payload := auth.Payload{Sub: subject, Exp: 4102444800}
	token, err := auth.SignToken(payload, secret)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(token)

	verified, err := auth.VerifyToken(token, secret)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if verified.Sub != subject {
		fmt.Fprintln(os.Stderr, "simple token subject changed")
		os.Exit(1)
	}
	fmt.Printf("verified subject %d\n", verified.Exp)

	_, err = auth.VerifyToken(token+"A", secret)
	if !errors.Is(err, auth.ErrInvalidSignature) {
		fmt.Fprintln(os.Stderr, "tampered token was not rejected as an invalid signature")
		os.Exit(1)
	}
	fmt.Println("tampered invalid-signature")

	expired, err := auth.SignToken(auth.Payload{Sub: "old", Exp: 1}, secret)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	_, err = auth.VerifyToken(expired, secret)
	if !errors.Is(err, auth.ErrExpiredToken) {
		fmt.Fprintln(os.Stderr, "expired token was not rejected")
		os.Exit(1)
	}
	fmt.Println("expired expired")

	_, err = auth.SignToken(payload, []byte("short"))
	if err == nil || !strings.Contains(err.Error(), "at least 32 bytes") {
		fmt.Fprintln(os.Stderr, "short secret was not rejected")
		os.Exit(1)
	}
	fmt.Println("short secret-too-short")

	service, err := auth.NewService(auth.Key{
		ID:        "primary",
		Secret:    secret,
		Algorithm: auth.AlgHMACSHA256,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	claims := auth.StandardClaims{
		Sub: subject, Exp: 4102444800, Iat: 1700000000,
		Jti: "session-1", Iss: "foundation", Aud: "developers",
	}
	signed, err := service.Sign(claims)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(signed.Token)
	verifiedClaims, err := service.Verify(signed.Token)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	standard := verifiedClaims.(auth.StandardClaims)
	if standard.Sub != subject {
		fmt.Fprintln(os.Stderr, "service token subject changed")
		os.Exit(1)
	}
	fmt.Printf("service subject %d %d %s %s %s\n", standard.Exp, standard.Iat,
		standard.Jti, standard.Iss, standard.Aud)
}

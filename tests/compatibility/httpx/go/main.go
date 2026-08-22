package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/httpx"
	"github.com/mirkobrombin/go-foundation/v2/core/resiliency"
)

type roundTripperFunc func(*http.Request) (*http.Response, error)

func (f roundTripperFunc) RoundTrip(request *http.Request) (*http.Response, error) {
	return f(request)
}

func response() *http.Response {
	return &http.Response{
		StatusCode: http.StatusOK,
		Body:       io.NopCloser(bytes.NewBufferString("ok")),
	}
}

func main() {
	headerClient := httpx.New(
		&http.Client{Transport: roundTripperFunc(func(request *http.Request) (*http.Response, error) {
			fmt.Println("header", request.Header.Get("X-Test"), "original", request.Header.Get("X-Original"))
			return response(), nil
		})},
		httpx.Header("X-Test", "1"),
	)
	headerRequest, err := http.NewRequest(http.MethodGet, "https://example.com/path", nil)
	if err != nil {
		panic(err)
	}
	headerRequest.Header.Set("X-Original", "1")
	headerResponse, err := headerClient.Do(headerRequest)
	if err != nil {
		panic(err)
	}
	headerResponse.Body.Close()

	calls := 0
	retryClient := httpx.New(&http.Client{Transport: roundTripperFunc(func(request *http.Request) (*http.Response, error) {
		calls++
		body, readErr := io.ReadAll(request.Body)
		if readErr != nil {
			return nil, readErr
		}
		if calls < 3 {
			return nil, errors.New("temporary")
		}
		fmt.Println("retry", calls, "body", string(body))
		return response(), nil
	})})
	retryClient.WithRetry(
		resiliency.WithAttempts(3),
		resiliency.WithDelay(0, 0),
	)
	retryRequest, err := http.NewRequest(
		http.MethodPost,
		"https://example.com/path",
		bytes.NewBufferString("payload"),
	)
	if err != nil {
		panic(err)
	}
	retryResponse, err := retryClient.Do(retryRequest)
	if err != nil {
		panic(err)
	}
	retryResponse.Body.Close()

	breaker := resiliency.NewCircuitBreaker(1, time.Hour)
	breakerClient := httpx.New(&http.Client{Transport: roundTripperFunc(func(*http.Request) (*http.Response, error) {
		return nil, errors.New("permanent")
	})})
	breakerClient.WithBreaker(breaker)
	firstRequest, _ := http.NewRequest(http.MethodGet, "https://example.com/path", nil)
	if _, err := breakerClient.Do(firstRequest); err == nil {
		panic("first breaker call succeeded")
	}
	secondRequest, _ := http.NewRequest(http.MethodGet, "https://example.com/path", nil)
	if _, err := breakerClient.Do(secondRequest); !errors.Is(err, resiliency.ErrCircuitOpen) {
		panic("breaker did not open")
	}
	fmt.Println("breaker open")
}

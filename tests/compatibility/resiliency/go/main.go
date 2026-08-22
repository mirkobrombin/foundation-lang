package main

import (
	"context"
	"errors"
	"fmt"
	"net/http/httptest"
	"os"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/app/web"
	"github.com/mirkobrombin/go-foundation/v2/core/resiliency"
)

var operationFailure = errors.New("operation failed")

func fail(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}

func status(server *web.Server, client string) int {
	request := httptest.NewRequest("GET", "/", nil)
	request.RemoteAddr = client + ":1000"
	response := httptest.NewRecorder()
	server.ServeHTTP(response, request)
	return response.Code
}

func main() {
	bucket, err := resiliency.NewRateLimiter(1, 2)
	if err != nil {
		fail(err)
	}
	if !bucket.Allow() || !bucket.Allow() || bucket.Allow() {
		fail(fmt.Errorf("initial token bucket state changed"))
	}
	time.Sleep(1100 * time.Millisecond)
	if !bucket.Allow() || bucket.Allow() {
		fail(fmt.Errorf("token bucket refill changed"))
	}
	if _, err := resiliency.NewRateLimiter(0, 1); err == nil {
		fail(fmt.Errorf("zero rate was accepted"))
	}
	if _, err := resiliency.NewRateLimiter(1, 0); err == nil {
		fail(fmt.Errorf("zero burst was accepted"))
	}

	middleware, err := web.RateLimit(1, 2)
	if err != nil {
		fail(err)
	}
	server := web.New()
	server.Use(middleware)
	if err := server.MapGet("/", func(ctx *web.Context) error {
		return ctx.String(200, "accepted")
	}); err != nil {
		fail(err)
	}

	fmt.Println(status(server, "192.0.2.1"))
	fmt.Println(status(server, "192.0.2.1"))
	fmt.Println(status(server, "192.0.2.1"))
	fmt.Println(status(server, "192.0.2.2"))
	fmt.Println("resiliency rate limit ok")

	waiter, err := resiliency.NewRateLimiter(1000, 1)
	if err != nil || !waiter.Allow() {
		fail(fmt.Errorf("wait limiter setup changed: %v", err))
	}
	if err := waiter.Wait(context.Background()); err != nil {
		fail(err)
	}
	fmt.Println("wait ok")

	retryCalls := 0
	err = resiliency.Retry(context.Background(), func() error {
		retryCalls++
		if retryCalls < 3 {
			return operationFailure
		}
		return nil
	}, resiliency.WithAttempts(3), resiliency.WithDelay(time.Millisecond, 2*time.Millisecond))
	if err != nil {
		fail(err)
	}
	fmt.Println("retry", retryCalls)

	retryCalls = 0
	err = resiliency.Retry(context.Background(), func() error {
		retryCalls++
		return operationFailure
	}, resiliency.WithAttempts(3), resiliency.WithDelay(0, 0),
		resiliency.WithRetryIf(func(error) bool { return false }))
	if !errors.Is(err, operationFailure) {
		fail(fmt.Errorf("retry filter changed: %v", err))
	}
	fmt.Println("retry filtered", retryCalls)

	changes := 0
	circuit := resiliency.NewCircuitBreaker(2, time.Millisecond)
	circuit.OnStateChange(func(resiliency.State, resiliency.State) { changes++ })
	firstCircuit := circuit.Execute(func() error { return operationFailure })
	secondCircuit := circuit.Execute(func() error { return operationFailure })
	openCircuit := circuit.Execute(func() error { return nil })
	time.Sleep(3 * time.Millisecond)
	recoveredCircuit := circuit.Execute(func() error { return nil })
	fmt.Println("circuit",
		errors.Is(firstCircuit, operationFailure),
		errors.Is(secondCircuit, operationFailure),
		errors.Is(openCircuit, resiliency.ErrCircuitOpen),
		recoveredCircuit == nil,
		circuit.State(),
		changes,
	)

	bulkhead, err := resiliency.NewBulkhead(1, 1)
	if err != nil {
		fail(err)
	}
	started := make(chan struct{})
	release := make(chan struct{})
	finished := make(chan error, 2)
	go func() {
		finished <- bulkhead.Execute(context.Background(), func() error {
			close(started)
			<-release
			return nil
		})
	}()
	<-started
	go func() {
		finished <- bulkhead.Execute(context.Background(), func() error { return nil })
	}()
	time.Sleep(10 * time.Millisecond)
	full := bulkhead.Execute(context.Background(), func() error { return nil })
	close(release)
	if err := <-finished; err != nil {
		fail(err)
	}
	if err := <-finished; err != nil {
		fail(err)
	}
	fmt.Println("bulkhead", errors.Is(full, resiliency.ErrBulkheadFull), 2)
}

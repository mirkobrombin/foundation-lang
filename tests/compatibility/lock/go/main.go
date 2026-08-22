package main

import (
	"context"
	"fmt"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/lock"
)

func main() {
	ctx := context.Background()
	locker := lock.NewInMemoryLocker()

	first, ok, err := locker.TryLock(ctx, "shared", 0)
	if err != nil || !ok {
		panic("first lock failed")
	}
	fmt.Println("first acquired")
	fmt.Println(first.Key())

	_, ok, err = locker.TryLock(ctx, "shared", 0)
	if err != nil || ok {
		panic("second lock result changed")
	}
	fmt.Println("second unavailable")

	if err := first.Release(ctx); err != nil {
		panic(err)
	}
	afterRelease, ok, err := locker.TryLock(ctx, "shared", 0)
	if err != nil || !ok {
		panic("lock after release failed")
	}
	fmt.Println("after release acquired")
	if err := afterRelease.Release(ctx); err != nil {
		panic(err)
	}

	expiring, ok, err := locker.TryLock(ctx, "expiring", 2*time.Millisecond)
	if err != nil || !ok {
		panic("expiring lock failed")
	}
	afterExpiry, err := locker.Acquire(ctx, "expiring", 0)
	if err != nil {
		panic(err)
	}
	fmt.Println("after ttl acquired")
	if err := expiring.Release(ctx); err != nil {
		panic(err)
	}
	if err := afterExpiry.Release(ctx); err != nil {
		panic(err)
	}

	old, ok, err := locker.TryLock(ctx, "owner", 10*time.Millisecond)
	if err != nil || !ok {
		panic("old owner lock failed")
	}
	if err := old.Release(ctx); err != nil {
		panic(err)
	}
	current, ok, err := locker.TryLock(ctx, "owner", 0)
	if err != nil || !ok {
		panic("current owner lock failed")
	}
	time.Sleep(20 * time.Millisecond)
	if err := old.Release(ctx); err != nil {
		panic(err)
	}
	_, ok, err = locker.TryLock(ctx, "owner", 0)
	if err != nil || ok {
		panic("old owner released current owner")
	}
	fmt.Println("new owner protected")
	if err := current.Release(ctx); err != nil {
		panic(err)
	}

	fmt.Println("lock compatibility ok")
}

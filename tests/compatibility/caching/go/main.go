package main

import (
	"context"
	"fmt"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/caching"
)

type profile struct {
	Name string
	Age  int32
}

func profileText(value profile, found bool) string {
	if !found {
		return "miss"
	}
	return fmt.Sprintf("%s:%d", value.Name, value.Age)
}

func main() {
	ctx := context.Background()
	memory := caching.NewInMemory[profile]()
	value, found, err := memory.Get(ctx, "profile")
	if err != nil {
		panic(err)
	}
	fmt.Println("memory:" + profileText(value, found))
	if err := memory.Set(ctx, "profile", profile{Name: "Ada", Age: 36}, 0); err != nil {
		panic(err)
	}
	value, found, err = memory.Get(ctx, "profile")
	if err != nil {
		panic(err)
	}
	fmt.Println("memory:" + profileText(value, found))
	if err := memory.Set(ctx, "profile", profile{Name: "Grace", Age: 37}, 0); err != nil {
		panic(err)
	}
	value, found, err = memory.Get(ctx, "profile")
	if err != nil {
		panic(err)
	}
	fmt.Println("memory:" + profileText(value, found))
	if err := memory.Invalidate(ctx, "profile"); err != nil {
		panic(err)
	}
	value, found, err = memory.Get(ctx, "profile")
	if err != nil {
		panic(err)
	}
	fmt.Println("memory:" + profileText(value, found))

	expiring := caching.NewInMemory[profile](
		caching.WithTTL[profile](2 * time.Millisecond),
		caching.WithMaxEntries[profile](8),
	)
	if err := expiring.Set(ctx, "short", profile{Name: "Short", Age: 1}, 0); err != nil {
		panic(err)
	}
	time.Sleep(3 * time.Millisecond)
	value, found, err = expiring.Get(ctx, "short")
	if err != nil {
		panic(err)
	}
	fmt.Println("ttl:" + profileText(value, found))

	bounded := caching.NewInMemory[profile](caching.WithMaxEntries[profile](1))
	if err := bounded.Set(ctx, "one", profile{Name: "One", Age: 1}, 0); err != nil {
		panic(err)
	}
	if err := bounded.Set(ctx, "two", profile{Name: "Two", Age: 2}, 0); err != nil {
		panic(err)
	}
	fmt.Printf("bounded:%d\n", bounded.Len())

	backend := caching.NewDistributedInMemory()
	if err := backend.Set(ctx, "raw", []byte("Foundation"), 0); err != nil {
		panic(err)
	}
	raw, found, err := backend.Get(ctx, "raw")
	if err != nil || !found {
		panic("missing distributed value")
	}
	fmt.Println("bytes:" + string(raw))

	bridge := caching.NewDistributedBridge[profile](backend)
	if err := bridge.Set(ctx, "typed", profile{Name: "Ada", Age: 37}, 0); err != nil {
		panic(err)
	}
	value, found, err = bridge.Get(ctx, "typed")
	if err != nil {
		panic(err)
	}
	fmt.Println("bridge:" + profileText(value, found))
	if err := bridge.Invalidate(ctx, "typed"); err != nil {
		panic(err)
	}
	value, found, err = bridge.Get(ctx, "typed")
	if err != nil {
		panic(err)
	}
	fmt.Println("bridge:" + profileText(value, found))
}

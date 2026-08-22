package main

import (
	"fmt"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/safemap"
)

func main() {
	mapping := safemap.New[string, int]()
	mapping.Set("one", 1)
	mapping.Set("two", 2)
	value, found := mapping.Get("one")
	if !found {
		value = -1
	}
	fmt.Println(value)
	fmt.Println(mapping.Has("two"))
	fmt.Println(mapping.Len())
	fmt.Println(mapping.GetOrSet("one", 9))
	fmt.Println(mapping.Compute("one", func(value int, exists bool) int {
		return value + 1
	}))
	mapping.Delete("two")
	fmt.Println(!mapping.Has("two"))
	fmt.Println(len(mapping.Keys()))
	fmt.Println(len(mapping.Values()))
	count := 0
	mapping.Range(func(key string, value int) bool {
		count++
		return true
	})
	fmt.Println(count)
	mapping.Clear()
	fmt.Println(mapping.Len())
	fmt.Println(safemap.StringHasher("hello"))

	sharded := safemap.NewSharded[string, int](safemap.StringHasher, 3).
		WithExpiry(time.Millisecond)
	sharded.Set("short", 7)
	fmt.Println(sharded.Has("short"))
	time.Sleep(2 * time.Millisecond)
	fmt.Println(sharded.Has("short"))
	fmt.Println("safemap compatibility ok")
}

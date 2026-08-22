package main

import (
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/pooling"
)

func main() {
	pool := pooling.New(
		func() []int { return []int{1} },
		pooling.WithMaxSize[[]int](1),
		pooling.WithFinalizer(func(value []int) { value[0] = 9 }),
	)

	pool.Put([]int{10})
	pool.Put([]int{20})
	refreshed := pool.Get()
	created := pool.Get()
	fmt.Printf("reused=%d\n", refreshed[0])
	fmt.Printf("created=%d\n", created[0])
}

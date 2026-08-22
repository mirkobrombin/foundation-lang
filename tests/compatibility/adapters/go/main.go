package main

import (
	"fmt"
	"sort"

	"github.com/mirkobrombin/go-foundation/v2/core/adapters"
)

func main() {
	registry := adapters.NewRegistry[int32]()
	registry.OnRegister(func(name string, value int32) {
		found, ok := registry.Get(name)
		if !ok || found != value {
			panic("register callback could not reenter")
		}
		fmt.Printf("first:register:%s:%d\n", name, value)
	})
	registry.OnRegister(func(name string, value int32) {
		found, ok := registry.Get(name)
		if !ok || found != value {
			panic("register callback could not reenter")
		}
		fmt.Printf("second:register:%s:%d\n", name, value)
	})
	registry.OnRemove(func(name string) {
		if registry.Has(name) {
			panic("remove callback could not reenter")
		}
		fmt.Println("first:remove:" + name)
	})
	registry.OnRemove(func(name string) {
		if registry.Has(name) {
			panic("remove callback could not reenter")
		}
		fmt.Println("second:remove:" + name)
	})

	registry.Register("alpha", 1)
	registry.Register("beta", 2)
	value, ok := registry.Get("alpha")
	if !ok {
		panic("alpha adapter missing")
	}
	fmt.Printf("get:alpha:%d\n", value)
	registry.SetDefault("beta")
	fmt.Printf("default:%d\n", registry.Default())
	names := registry.Names()
	sort.Strings(names)
	for _, name := range names {
		fmt.Println("name:" + name)
	}

	registry.Remove("alpha")
	registry.Remove("missing")
	fmt.Printf("has:alpha:%t\n", registry.Has("alpha"))
	registry.Clear()
	registry.Register("gamma", 3)
	fmt.Printf("fallback:%d\n", registry.DefaultOr(9))
}

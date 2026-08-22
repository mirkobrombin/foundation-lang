package main

import (
	"context"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/saga"
)

func main() {
	workflow := saga.New()
	workflow.AddGroup(saga.Group{
		{Name: "left", Do: func(context.Context) error { return nil }},
		{Name: "right", Do: func(context.Context) error { return nil }},
	})
	if err := workflow.Run(context.Background()); err != nil {
		panic(err)
	}
	fmt.Println("saga pinned v2 ok")
}

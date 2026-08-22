package main

import (
	"context"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/app"
	"github.com/mirkobrombin/go-foundation/v2/app/hosting"
)

func main() {
	application := app.New()
	if _, err := application.Build(); err != nil {
		panic(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	go cancel()
	host := hosting.NewBuilder().Build()
	if err := host.Run(ctx); err != nil {
		panic(err)
	}
	fmt.Println("app composition ok")
}

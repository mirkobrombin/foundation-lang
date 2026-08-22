package main

import (
	"context"
	"fmt"
	"os"

	"github.com/mirkobrombin/go-foundation/v2/core/configuration"
	"github.com/mirkobrombin/go-foundation/v2/core/configuration/source/dir"
)

func main() {
	if len(os.Args) != 2 {
		os.Exit(1)
	}
	cfg, err := configuration.NewBuilder().
		Add(dir.New(os.Args[1], "*.json", "conf.global.json")).
		Build(context.Background())
	if err != nil {
		panic(err)
	}
	alphaHost, ok := cfg.GetString("alpha:host")
	if !ok {
		os.Exit(2)
	}
	alphaPort, ok := cfg.GetInt("alpha:port")
	if !ok {
		os.Exit(3)
	}
	betaHost, ok := cfg.GetString("beta:host")
	if !ok {
		os.Exit(4)
	}
	betaPort, ok := cfg.GetInt("beta:port")
	if !ok {
		os.Exit(5)
	}
	fmt.Println(alphaHost)
	fmt.Println(alphaPort)
	fmt.Println(betaHost)
	fmt.Println(betaPort)
}

package main

import (
    "context"
    "fmt"
    "os"

    "github.com/mirkobrombin/go-foundation/v2/core/configuration"
    "github.com/mirkobrombin/go-foundation/v2/core/configuration/source/env"
    "github.com/mirkobrombin/go-foundation/v2/core/configuration/source/file"
)

func main() {
    if len(os.Args) != 2 {
        os.Exit(1)
    }
    cfg, err := configuration.NewBuilder().
        Add(file.New(os.Args[1])).
        Add(env.New("FOUNDATION_CONFIGURATION_")).
        Build(context.Background())
    if err != nil {
        panic(err)
    }
    host, ok := cfg.GetString("host")
    if !ok {
        os.Exit(2)
    }
    port, ok := cfg.GetInt("port")
    if !ok {
        os.Exit(3)
    }
    fmt.Println(host)
    fmt.Println(port)
}

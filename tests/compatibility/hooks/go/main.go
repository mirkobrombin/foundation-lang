package main

import (
	"context"
	"errors"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/hooks"
)

var errBlocked = errors.New("blocked")

func printHook(label string) hooks.HookFunc {
	return func(context.Context, string, []any) error {
		fmt.Println(label)
		return nil
	}
}

func block(context.Context, string, []any) error {
	fmt.Println("blocked")
	return errBlocked
}

func main() {
	runner := hooks.NewRunner()
	runner.BeforeAll(printHook("before all"))
	runner.Before("save", printHook("before save"))
	runner.AfterAll(printHook("after all"))
	runner.After("save", printHook("after save"))
	if err := runner.Run(context.Background(), "save", func() error {
		fmt.Println("action")
		return nil
	}); err != nil {
		panic(err)
	}

	runner.Before("fail", block)
	if err := runner.Run(context.Background(), "fail", func() error {
		fmt.Println("skipped")
		return nil
	}); !errors.Is(err, errBlocked) {
		panic("expected blocked hook")
	}

	runner.Clear()
	if err := runner.Run(context.Background(), "save", func() error {
		fmt.Println("cleared")
		return nil
	}); err != nil {
		panic(err)
	}
}

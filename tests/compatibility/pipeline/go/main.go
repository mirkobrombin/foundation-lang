package main

import (
	"context"
	"errors"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/pipeline"
)

func main() {
	chain := pipeline.New[string, string]().
		Use(func(ctx context.Context, input string, next func(context.Context, string) (string, error)) (string, error) {
			output, err := next(ctx, input+"a")
			return output + "A", err
		}).
		Use(func(ctx context.Context, input string, next func(context.Context, string) (string, error)) (string, error) {
			output, err := next(ctx, input+"b")
			return output + "B", err
		}).
		Then(func(ctx context.Context, input string) (string, error) {
			return input + "h", nil
		})
	output, err := chain.Process(context.Background(), "")
	if err != nil {
		panic(err)
	}
	fmt.Println(output)

	calls := 0
	stateful := pipeline.New[string, string]().
		Use(func(ctx context.Context, input string, next func(context.Context, string) (string, error)) (string, error) {
			calls++
			output, err := next(ctx, input+"s")
			return fmt.Sprintf("%s%d", output, calls), err
		}).
		Then(func(ctx context.Context, input string) (string, error) {
			return input + "h", nil
		})
	first, err := stateful.Process(context.Background(), "x")
	if err != nil {
		panic(err)
	}
	second, err := stateful.Process(context.Background(), "x")
	if err != nil {
		panic(err)
	}
	fmt.Println(first)
	fmt.Println(second)

	stopped := pipeline.New[string, string]().
		Use(func(ctx context.Context, input string, next func(context.Context, string) (string, error)) (string, error) {
			return "", errors.New("stopped")
		}).
		Then(func(ctx context.Context, input string) (string, error) {
			panic("terminal called after short circuit")
		})
	if _, err := stopped.Process(context.Background(), "ignored"); err == nil {
		panic("missing short-circuit error")
	}
	fmt.Println("stopped")
}

package main

import (
	"context"
	"errors"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/tracing"
)

type contextKey string

const (
	traceKey contextKey = "trace"
	spanKey  contextKey = "span"
)

type recordingTracer struct{}

func (recordingTracer) StartSpan(ctx context.Context, name string) (context.Context, tracing.Span) {
	fmt.Println("name=" + name)
	fmt.Printf("parent=%s/%s\n", ctx.Value(traceKey), ctx.Value(spanKey))
	return context.WithValue(ctx, spanKey, "child"), recordingSpan{}
}

type recordingSpan struct{}

func (recordingSpan) SetAttributes(attributes ...tracing.Attribute) {
	fmt.Printf("attributes=%d\n", len(attributes))
}

func (recordingSpan) RecordError(err error) {
	fmt.Println("error=" + err.Error())
}

func (recordingSpan) End() {
	fmt.Println("ended=true")
}

func main() {
	parent := context.WithValue(context.Background(), traceKey, "trace")
	parent = context.WithValue(parent, spanKey, "parent")
	_, span := tracing.StartSpan(parent, recordingTracer{}, "checkout")
	span.SetAttributes(tracing.Attribute{Key: "component", Value: "checkout"})
	span.RecordError(errors.New("failed"))
	span.End()

	_, silent := tracing.StartSpan(context.Background(), nil, "ignored")
	silent.SetAttributes()
	silent.RecordError(errors.New("ignored"))
	silent.End()
	fmt.Println("noop=ok")
}

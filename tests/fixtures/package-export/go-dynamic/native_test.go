package sample_native

import (
	"math"
	"os"
	"testing"
)

func TestCallsFoundationSharedLibrary(t *testing.T) {
	path := os.Getenv("FOUNDATION_LIBRARY")
	if path == "" {
		t.Fatal("FOUNDATION_LIBRARY is not set")
	}
	library, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := library.Close(); err != nil {
			t.Error(err)
		}
	})

	if got := library.FoundationIncrement(41); got != 42 {
		t.Fatalf("FoundationIncrement(41) = %d, want 42", got)
	}
	if got := library.FoundationInvoke(func(value int32) int32 { return value + 1 }, 20); got != 21 {
		t.Fatalf("FoundationInvoke(callback, 20) = %d, want 21", got)
	}
	if got := library.FoundationSine(math.Pi / 2); math.Abs(got-1) > 1e-12 {
		t.Fatalf("FoundationSine(pi/2) = %g, want 1", got)
	}
}

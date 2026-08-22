package sample_native

import (
	"math"
	"testing"
)

func TestCallsFoundationArchive(t *testing.T) {
	if got := FoundationIncrement(41); got != 42 {
		t.Fatalf("FoundationIncrement(41) = %d, want 42", got)
	}
	if got := FoundationRoundTrip(17); got != 17 {
		t.Fatalf("FoundationRoundTrip(17) = %d, want 17", got)
	}
	if got := FoundationSine(math.Pi / 2); math.Abs(got-1) > 1e-12 {
		t.Fatalf("FoundationSine(pi/2) = %g, want 1", got)
	}
}

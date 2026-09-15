package sample_source

import (
	"bytes"
	"errors"
	"io"
	"math"
	"os"
	"os/exec"
	"strconv"
	"testing"
)

func TestTranslatedFoundationSource(t *testing.T) {
	if got := Add(20, 22); got != 42 {
		t.Fatalf("Add(20, 22) = %d, want 42", got)
	}
	if got := Clamp(99, 10, 40); got != 40 {
		t.Fatalf("Clamp(99, 10, 40) = %d, want 40", got)
	}
	if got := TwicePlus(20, 2); got != 42 {
		t.Fatalf("TwicePlus(20, 2) = %d, want 42", got)
	}
	if got := SumTo(10); got != 45 {
		t.Fatalf("SumTo(10) = %d, want 45", got)
	}
	if !IsWithin(20, 10, 30) || IsWithin(40, 10, 30) {
		t.Fatal("IsWithin returned the wrong boundary result")
	}
	if got := Subtract(50, 8); got != 42 {
		t.Fatalf("Subtract(50, 8) = %d, want 42", got)
	}
	if got := Multiply(7, 6); got != 42 {
		t.Fatalf("Multiply(7, 6) = %d, want 42", got)
	}
	if got := Divide(84, 2); got != 42 {
		t.Fatalf("Divide(84, 2) = %d, want 42", got)
	}
	if got := Remainder(86, 44); got != 42 {
		t.Fatalf("Remainder(86, 44) = %d, want 42", got)
	}
	if got := ShiftLeft(21, 1); got != 42 {
		t.Fatalf("ShiftLeft(21, 1) = %d, want 42", got)
	}
	if got := ShiftRight(-84, 1); got != -42 {
		t.Fatalf("ShiftRight(-84, 1) = %d, want -42", got)
	}
	if got := ShiftUnsigned(128, 7); got != 1 {
		t.Fatalf("ShiftUnsigned(128, 7) = %d, want 1", got)
	}
	if got := ShiftCompound(21, 1); got != 42 {
		t.Fatalf("ShiftCompound(21, 1) = %d, want 42", got)
	}
	for _, test := range []struct {
		name      string
		operation func() int32
	}{
		{name: "overflow", operation: ShiftOverflow},
		{name: "invalid count", operation: ShiftInvalidCount},
	} {
		panicked := false
		func() {
			defer func() { panicked = recover() != nil }()
			_ = test.operation()
		}()
		if !panicked {
			t.Fatalf("%s shift did not panic", test.name)
		}
	}
	if got := Negate(-42); got != 42 {
		t.Fatalf("Negate(-42) = %d, want 42", got)
	}
	if got := AddUnsigned(40, 2); got != 42 {
		t.Fatalf("AddUnsigned(40, 2) = %d, want 42", got)
	}
	if got := SubtractUnsigned(44, 2); got != 42 {
		t.Fatalf("SubtractUnsigned(44, 2) = %d, want 42", got)
	}
	if got := NameSafety(40, 1, 1); got != 42 {
		t.Fatalf("NameSafety(40, 1, 1) = %d, want 42", got)
	}
	if got := Greet("Foundation"); got != "Hello, Foundation" {
		t.Fatalf("Greet(Foundation) = %q, want %q", got, "Hello, Foundation")
	}
	if got := Greet(""); got != "Hello, stranger" {
		t.Fatalf("Greet(empty) = %q, want %q", got, "Hello, stranger")
	}
	if got := TextLength("héllo"); got != 6 {
		t.Fatalf("TextLength(héllo) = %d, want UTF-8 byte length 6", got)
	}
	if got := QuoteLine("Foundation"); got != "line 1\n\"Foundation\"\\end" {
		t.Fatalf("QuoteLine(Foundation) = %q", got)
	}
	if !SameText("same", "same") || SameText("same", "different") {
		t.Fatal("SameText returned the wrong equality result")
	}
	if got := Accent(); got != "héllo 🙂" {
		t.Fatalf("Accent() = %q, want %q", got, "héllo 🙂")
	}
	profile := NewProfile("Foundation", 40)
	if profile.Name != "Foundation" || profile.Score != 40 {
		t.Fatalf("NewProfile returned %#v", profile)
	}
	if got := profile.Display(); got != "Foundation" {
		t.Fatalf("profile.Display() = %q", got)
	}
	profile.Rename("Foundation Lang")
	profile.AddScore(1)
	profile.BumpTwice()
	if got := profile.WithBonus(-1); got != 42 {
		t.Fatalf("profile.WithBonus(-1) = %d, want 42", got)
	}
	if profile.Score != 43 {
		t.Fatalf("profile.Score = %d, want 43", profile.Score)
	}
	if got := MethodScore(profile, -1); got != 42 {
		t.Fatalf("MethodScore(profile, -1) = %d, want 42", got)
	}
	origin := ProfileOrigin()
	if origin.Name != "origin" || origin.Score != 0 {
		t.Fatalf("ProfileOrigin returned %#v", origin)
	}
	emptyProfile := NewProfileEmpty()
	if emptyProfile.Name != "" || emptyProfile.Score != 0 {
		t.Fatalf("NewProfileEmpty returned %#v", emptyProfile)
	}
	checkedProfile := NewProfileChecked("Foundation", 42)
	if value, ok := checkedProfile.GetOk(); !ok || value.Score != 42 {
		t.Fatalf("NewProfileChecked returned (%#v, %v)", value, ok)
	}
	invalidProfile := NewProfileChecked("", 42)
	if value, ok := invalidProfile.GetErr(); !ok || !value.IsEmptyName() {
		t.Fatal("NewProfileChecked(empty) did not return EmptyName")
	}
	madeProfile := MakeProfile("Foundation Lang", 40)
	madeProfile = RenameProfile(madeProfile, "Foundation Lang")
	if got := ProfileName(madeProfile); got != "Foundation Lang" {
		t.Fatalf("ProfileName(madeProfile) = %q", got)
	}
	if got := IncreaseScore(madeProfile, 2); got != 42 {
		t.Fatalf("IncreaseScore(madeProfile, 2) = %d, want 42", got)
	}
	if got := ChooseLabel(true); got != "Foundation" {
		t.Fatalf("ChooseLabel(true) = %q, want Foundation", got)
	}
	if got := ChooseLabel(false); got != "fallback" {
		t.Fatalf("ChooseLabel(false) = %q, want fallback", got)
	}
	if got := ChooseNumber(true); got != 42 {
		t.Fatalf("ChooseNumber(true) = %d, want 42", got)
	}
	if got := ChooseNumber(false); got != 7 {
		t.Fatalf("ChooseNumber(false) = %d, want 7", got)
	}
	if got := ChooseNested(false, true); got != 21 {
		t.Fatalf("ChooseNested(false, true) = %d, want 21", got)
	}
	if got := ChooseNested(false, false); got != 0 {
		t.Fatalf("ChooseNested(false, false) = %d, want 0", got)
	}
	if got := LazyThen(); got != 42 {
		t.Fatalf("LazyThen() = %d, want 42", got)
	}
	if got := LazyElse(); got != 42 {
		t.Fatalf("LazyElse() = %d, want 42", got)
	}
	conditionalProfile := NewProfile("condition", 0)
	if got := conditionalProfile.ChooseAndBump(); got != 42 || conditionalProfile.Score != 1 {
		t.Fatalf("ChooseAndBump() = %d with score %d", got, conditionalProfile.Score)
	}
	replaceProfile := NewProfile("before", 0)
	if got := replaceProfile.ReplaceName("after"); got != "before" || replaceProfile.Name != "after" {
		t.Fatalf("ReplaceName() = %q with name %q", got, replaceProfile.Name)
	}
	replaceValues := []int32{10, 20}
	if got := replaceProfile.ReplaceSliceOrdered(replaceValues); got != 20 || replaceValues[1] != 42 || replaceProfile.Score != 12 {
		t.Fatalf("ReplaceSliceOrdered() = %d with values %#v and score %d", got, replaceValues, replaceProfile.Score)
	}
	panicProfile := NewProfile("panic", 0)
	panicValues := []int32{10, 20}
	panicked := false
	func() {
		defer func() {
			panicked = recover() != nil
		}()
		_ = panicProfile.ReplacePanicOrder(panicValues)
	}()
	if !panicked || panicProfile.Score != 0 || panicValues[1] != 20 {
		t.Fatalf("ReplacePanicOrder() left panic=%v, score=%d, values=%#v", panicked, panicProfile.Score, panicValues)
	}
	if got := ReplaceLocal(); got != "old:new" {
		t.Fatalf("ReplaceLocal() = %q, want old:new", got)
	}
	if got := ReplaceArrayElement(); got != 62 {
		t.Fatalf("ReplaceArrayElement() = %d, want 62", got)
	}
	destructureCounter := NewProfile("counter", 0)
	if got := destructureCounter.DestructureGenerated(); got != "Foundation" || destructureCounter.Score != 1 {
		t.Fatalf("DestructureGenerated() = %q with score %d", got, destructureCounter.Score)
	}
	if got := DestructureLocal(); got != 42 {
		t.Fatalf("DestructureLocal() = %d, want 42", got)
	}
	if got := FunctionNamed(); got != 42 {
		t.Fatalf("FunctionNamed() = %d, want 42", got)
	}
	if got := GenericDirect(); got != 42 {
		t.Fatalf("GenericDirect() = %d, want 42", got)
	}
	if got := GenericInferred(); got != 42 {
		t.Fatalf("GenericInferred() = %d, want 42", got)
	}
	if got := GenericText(); got != "Foundation" {
		t.Fatalf("GenericText() = %q, want Foundation", got)
	}
	if got := GenericFunctionValue(); got != 42 {
		t.Fatalf("GenericFunctionValue() = %d, want 42", got)
	}
	if got := GenericEdit(); got != 42 {
		t.Fatalf("GenericEdit() = %d, want 42", got)
	}
	numberBox := GenericStructNumber()
	if numberBox.Value != 42 {
		t.Fatalf("GenericStructNumber() = %#v", numberBox)
	}
	textBox := GenericStructText()
	if textBox.Value != "Foundation" {
		t.Fatalf("GenericStructText() = %#v", textBox)
	}
	if got := GenericStructMethods(); got != 42 {
		t.Fatalf("GenericStructMethods() = %d, want 42", got)
	}
	nestedBox := GenericStructNested()
	if nestedBox.Value.Value != 42 {
		t.Fatalf("GenericStructNested() = %#v", nestedBox)
	}
	if got := GenericStructDestructure(); got != 42 {
		t.Fatalf("GenericStructDestructure() = %d, want 42", got)
	}
	if got := GenericStructNameCollision(); got.Code != 42 {
		t.Fatalf("GenericStructNameCollision() = %#v", got)
	}
	directBox := NewBoxI32(40)
	directBox.Set(42)
	if got := directBox.Marker(); got != 42 || directBox.Value != 42 {
		t.Fatalf("NewBoxI32().Marker() = %d with value %d", got, directBox.Value)
	}
	if got := ApplyFunction(21, func(value int32) int32 { return value * 2 }); got != 42 {
		t.Fatalf("ApplyFunction() = %d, want 42", got)
	}
	if got := FunctionPayload(); got != 42 {
		t.Fatalf("FunctionPayload() = %d, want 42", got)
	}
	if got := FunctionEdit(); got != 42 {
		t.Fatalf("FunctionEdit() = %d, want 42", got)
	}
	if got := FunctionOwn(); got != "Foundation!" {
		t.Fatalf("FunctionOwn() = %q, want Foundation!", got)
	}
	if got := ClosureCopy(); got != 42 {
		t.Fatalf("ClosureCopy() = %d, want 42", got)
	}
	if got := ClosureEdit(); got != 42 {
		t.Fatalf("ClosureEdit() = %d, want 42", got)
	}
	if got := ClosureOwn(); got != "Foundation" {
		t.Fatalf("ClosureOwn() = %q, want Foundation", got)
	}
	if got := ClosureReturn(); got != 42 {
		t.Fatalf("ClosureReturn() = %d, want 42", got)
	}
	if got := ClosureFunctionCapture(); got != 44 {
		t.Fatalf("ClosureFunctionCapture() = %d, want 44", got)
	}
	if got := ClosureBorrowedEdit(); got != 42 {
		t.Fatalf("ClosureBorrowedEdit() = %d, want 42", got)
	}
	values := BuildNumbers()
	if values != [3]int32{10, 20, 30} {
		t.Fatalf("BuildNumbers() = %#v", values)
	}
	if got := SumFixed(values); got != 60 {
		t.Fatalf("SumFixed(values) = %d, want 60", got)
	}
	if got := SumSlice(values[:]); got != 40 {
		t.Fatalf("SumSlice(values) = %d, want 40", got)
	}
	if got := SumBuiltNumbers(); got != 40 {
		t.Fatalf("SumBuiltNumbers() = %d, want 40", got)
	}
	if got := SumTemporaryNumbers(); got != 40 {
		t.Fatalf("SumTemporaryNumbers() = %d, want 40", got)
	}
	words := BuildWords()
	if got := JoinWords(words[:]); got != "Foundation Lang" {
		t.Fatalf("JoinWords(words) = %q", got)
	}
	if got := EmptyNumbers(); got != [0]int32{} {
		t.Fatalf("EmptyNumbers() = %#v", got)
	}
	BumpSlice(values[:])
	if values != [3]int32{11, 21, 31} {
		t.Fatalf("BumpSlice(values) produced %#v", values)
	}
	if got := BumpAndSum(); got != 9 {
		t.Fatalf("BumpAndSum() = %d, want 9", got)
	}
	if !SequenceIsEmpty(nil) || SequenceIsEmpty(values[:]) {
		t.Fatal("SequenceIsEmpty returned the wrong result")
	}
	if got := FixedLength(values); got != 3 {
		t.Fatalf("FixedLength(values) = %d, want 3", got)
	}
	if got := SliceLength(values[:]); got != 3 {
		t.Fatalf("SliceLength(values) = %d, want 3", got)
	}
	matrix := [2][2]int32{{1, 2}, {42, 4}}
	if got := MatrixValue(matrix); got != 42 {
		t.Fatalf("MatrixValue(matrix) = %d, want 42", got)
	}
	if got := FirstProfileName([]Profile{madeProfile}); got != "Foundation Lang" {
		t.Fatalf("FirstProfileName(profile) = %q", got)
	}
	missing := MaybeName("")
	if !missing.IsNone() || missing.IsSome() {
		t.Fatal("MaybeName(empty) did not return None")
	}
	present := MaybeName("Foundation")
	if value, ok := present.GetSome(); !ok || value != "Foundation" {
		t.Fatalf("MaybeName(Foundation) = (%q, %v)", value, ok)
	}
	if got := ReadOptional(NewOptionStringNone()); got != "none" {
		t.Fatalf("ReadOptional(None) = %q", got)
	}
	if got := ReadOptional(NewOptionStringSome("Foundation")); got != "Foundation" {
		t.Fatalf("ReadOptional(Some) = %q", got)
	}
	if got := DescribeOptional(NewOptionStringSome("Foundation")); got != "foundation" {
		t.Fatalf("DescribeOptional(Foundation) = %q", got)
	}
	if got := DescribeOptional(NewOptionStringSome("other")); got != "other" {
		t.Fatalf("DescribeOptional(other) = %q", got)
	}
	if got := RenderOrFallback(""); got != "fallback" {
		t.Fatalf("RenderOrFallback(empty) = %q", got)
	}
	if got := RenderOrFallback("Foundation"); got != "Hello, Foundation" {
		t.Fatalf("RenderOrFallback(Foundation) = %q", got)
	}
	success := RenderGreeting("Foundation")
	if value, ok := success.GetOk(); !ok || value != "Hello, Foundation" {
		t.Fatalf("RenderGreeting(Foundation) = (%q, %v)", value, ok)
	}
	failure := RenderGreeting("")
	if value, ok := failure.GetErr(); !ok || !value.IsEmptyName() {
		t.Fatal("RenderGreeting(empty) did not return EmptyName")
	}
	if !IsValidName("Foundation") || IsValidName("") {
		t.Fatal("IsValidName returned the wrong Result<void, GreetingError> result")
	}
	if got := DescribeError(NewGreetingErrorRejected("policy")); got != "rejected: policy" {
		t.Fatalf("DescribeError(Rejected) = %q", got)
	}
	if got := Remainder(-2_147_483_648, -1); got != 0 {
		t.Fatalf("Remainder(min, -1) = %d, want 0", got)
	}
}

func TestTranslatedSliceBoundsRemainChecked(t *testing.T) {
	panicked := false
	func() {
		defer func() {
			panicked = recover() != nil
		}()
		_ = FirstSlice(nil)
	}()
	if !panicked {
		t.Fatal("FirstSlice(nil) did not panic")
	}
}

func TestTranslatedArithmeticKeepsFoundationOverflowChecks(t *testing.T) {
	tests := map[string]func(){
		"add":               func() { _ = Add(2_147_483_647, 1) },
		"subtract":          func() { _ = Subtract(-2_147_483_648, 1) },
		"multiply":          func() { _ = Multiply(2_000_000_000, 2) },
		"divide":            func() { _ = Divide(-2_147_483_648, -1) },
		"divide by zero":    func() { _ = Divide(1, 0) },
		"remainder by zero": func() { _ = Remainder(1, 0) },
		"negate":            func() { _ = Negate(-2_147_483_648) },
		"unsigned add":      func() { _ = AddUnsigned(4_294_967_295, 1) },
		"unsigned subtract": func() { _ = SubtractUnsigned(0, 1) },
	}
	for name, test := range tests {
		t.Run(name, func(t *testing.T) {
			defer func() {
				if recover() == nil {
					t.Fatal("operation did not panic")
				}
			}()
			test()
		})
	}
}

func TestTranslatedPrintWritesFoundationLines(t *testing.T) {
	reader, writer, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = writer
	PrintLines("h\xc3\xa9llo")
	os.Stdout = stdout
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	got, err := io.ReadAll(reader)
	if err != nil {
		t.Fatal(err)
	}
	want := []byte("h\xc3\xa9llo Foundation\n\ntab\tquote\"\n")
	if !bytes.Equal(got, want) {
		t.Fatalf("PrintLines wrote %q, want %q", got, want)
	}
}

var panicCases = map[string]func(){
	"statement":   func() { PrintAndPanic() },
	"branch":      func() { _ = CheckedDivide(1, 0) },
	"return":      func() { _ = RequirePositive(0) },
	"match":       func() { _ = RequireSome(NewOptionI32None()) },
	"conditional": func() { _ = RequireReady(false) },
	"else":        func() { _ = RequireNarrow(1 << 40) },
}

func TestTranslatedPanicTerminatesProcess(t *testing.T) {
	if name := os.Getenv("FOUNDATION_PANIC_CASE"); name != "" {
		defer func() {
			if recover() != nil {
				_, _ = os.Stdout.WriteString("recovered\n")
			}
		}()
		panicCases[name]()
		return
	}
	if got := CheckedDivide(84, 2); got != 42 {
		t.Fatalf("CheckedDivide(84, 2) = %d, want 42", got)
	}
	if got := RequirePositive(42); got != 42 {
		t.Fatalf("RequirePositive(42) = %d, want 42", got)
	}
	if got := RequireSome(NewOptionI32Some(42)); got != 42 {
		t.Fatalf("RequireSome(Some(42)) = %d, want 42", got)
	}
	if got := RequireReady(true); got != 42 {
		t.Fatalf("RequireReady(true) = %d, want 42", got)
	}
	if got := RequireNarrow(42); got != 42 {
		t.Fatalf("RequireNarrow(42) = %d, want 42", got)
	}
	tests := map[string]struct {
		stdout string
		stderr string
	}{
		"statement":   {"before panic\n", "foundation panic: stop\n"},
		"branch":      {"", "foundation panic: division rejected: zero\n"},
		"return":      {"", "foundation panic: value must be positive\n"},
		"match":       {"", "foundation panic: missing value\n"},
		"conditional": {"", "foundation panic: not ready\n"},
		"else":        {"", "foundation panic: conversion failed\n"},
	}
	for name, want := range tests {
		t.Run(name, func(t *testing.T) {
			command := exec.Command(os.Args[0], "-test.run=^TestTranslatedPanicTerminatesProcess$")
			command.Env = append(os.Environ(), "FOUNDATION_PANIC_CASE="+name)
			var stdout, stderr bytes.Buffer
			command.Stdout = &stdout
			command.Stderr = &stderr
			err := command.Run()
			var exit *exec.ExitError
			if !errors.As(err, &exit) || exit.ExitCode() != 1 {
				t.Fatalf("panic exit = %v, want status 1", err)
			}
			if stdout.String() != want.stdout || stderr.String() != want.stderr {
				t.Fatalf("panic wrote stdout %q and stderr %q", stdout.String(), stderr.String())
			}
		})
	}
}

type numberResult interface {
	GetErr() (NumberError, bool)
}

func conversionFailure(result numberResult) string {
	failure, failed := result.GetErr()
	switch {
	case !failed:
		return "Ok"
	case failure.IsOutOfRange():
		return "OutOfRange"
	case failure.IsNonFinite():
		return "NonFinite"
	case failure.IsPrecisionLoss():
		return "PrecisionLoss"
	}
	return "invalid"
}

func TestTranslatedNumericConversionsMatchFoundation(t *testing.T) {
	if WidenSigned(-2_147_483_648) != -2_147_483_648 || WidenUnsigned(255) != 255 ||
		WidenFloat(0.1) != float64(float32(0.1)) || ExactFloat(-2_147_483_648) != -2_147_483_648 {
		t.Fatal("an infallible conversion changed its value")
	}
	nan := math.NaN()
	infinity := math.Inf(1)
	pointerHalf := math.Ldexp(1, strconv.IntSize-1)
	pointerLimit := math.Ldexp(1, strconv.IntSize)
	tests := []struct {
		name   string
		result numberResult
		want   string
	}{
		{"i64 max i32", NarrowSigned(2_147_483_647), "Ok"},
		{"i64 above i32", NarrowSigned(2_147_483_648), "OutOfRange"},
		{"i64 min i32", NarrowSigned(-2_147_483_648), "Ok"},
		{"i64 below i32", NarrowSigned(-2_147_483_649), "OutOfRange"},
		{"negative to u32", SignedToUnsigned(-1), "OutOfRange"},
		{"i32 max to u32", SignedToUnsigned(2_147_483_647), "Ok"},
		{"u64 above i64", UnsignedToSigned(1 << 63), "OutOfRange"},
		{"i64 max from u64", UnsignedToSigned(1<<63 - 1), "Ok"},
		{"NaN to i32", FloatToSigned(nan), "NonFinite"},
		{"infinity to i32", FloatToSigned(-infinity), "NonFinite"},
		{"f64 above i32", FloatToSigned(2_147_483_648), "OutOfRange"},
		{"f64 max i32", FloatToSigned(2_147_483_647), "Ok"},
		{"f64 min i32", FloatToSigned(-2_147_483_648), "Ok"},
		{"f64 below i32", FloatToSigned(-2_147_483_649), "OutOfRange"},
		{"fraction to i32", FloatToSigned(1.5), "PrecisionLoss"},
		{"negative fraction to i32", FloatToSigned(-0.5), "PrecisionLoss"},
		{"negative zero to i32", FloatToSigned(math.Copysign(0, -1)), "Ok"},
		{"f32 max u8", FloatToUnsigned(255), "Ok"},
		{"f32 above u8", FloatToUnsigned(256), "OutOfRange"},
		{"negative fraction to u8", FloatToUnsigned(-0.5), "OutOfRange"},
		{"negative zero to u8", FloatToUnsigned(float32(math.Copysign(0, -1))), "Ok"},
		{"f32 NaN to u8", FloatToUnsigned(float32(nan)), "NonFinite"},
		{"negative to usize", FloatToSize(-1), "OutOfRange"},
		{"usize limit", FloatToSize(pointerLimit), "OutOfRange"},
		{"usize high bit", FloatToSize(pointerHalf), "Ok"},
		{"isize limit", FloatToIsize(pointerHalf), "OutOfRange"},
		{"isize min", FloatToIsize(-pointerHalf), "Ok"},
		{"exact i64 to f64", SignedToFloat(1 << 53), "Ok"},
		{"inexact i64 to f64", SignedToFloat(1<<53 + 1), "PrecisionLoss"},
		{"i64 max to f64", SignedToFloat(math.MaxInt64), "PrecisionLoss"},
		{"i64 min to f64", SignedToFloat(math.MinInt64), "Ok"},
		{"usize max to f32", SizeToFloat(math.MaxUint), "PrecisionLoss"},
		{"exact usize to f32", SizeToFloat(1 << 24), "Ok"},
		{"inexact usize to f32", SizeToFloat(1<<24 + 1), "PrecisionLoss"},
		{"half to f32", NarrowFloat(0.5), "Ok"},
		{"tenth to f32", NarrowFloat(0.1), "PrecisionLoss"},
		{"f32 max", NarrowFloat(math.MaxFloat32), "Ok"},
		{"above f32 max", NarrowFloat(math.Nextafter(math.MaxFloat32, infinity)), "PrecisionLoss"},
		{"f32 overflow midpoint", NarrowFloat(0x1.ffffffp127), "OutOfRange"},
		{"below overflow midpoint", NarrowFloat(math.Nextafter(0x1.ffffffp127, 0)), "PrecisionLoss"},
		{"f64 above f32", NarrowFloat(1e39), "OutOfRange"},
		{"f64 NaN to f32", NarrowFloat(nan), "NonFinite"},
		{"f64 infinity to f32", NarrowFloat(-infinity), "NonFinite"},
		{"f32 underflow", NarrowFloat(1e-50), "PrecisionLoss"},
	}
	for _, test := range tests {
		if got := conversionFailure(test.result); got != test.want {
			t.Errorf("%s = %s, want %s", test.name, got, test.want)
		}
	}
	if value, ok := NarrowSigned(-2_147_483_648).GetOk(); !ok || value != -2_147_483_648 {
		t.Fatalf("NarrowSigned(min) = (%d, %v)", value, ok)
	}
	if value, ok := UnsignedToSigned(1<<63 - 1).GetOk(); !ok || value != math.MaxInt64 {
		t.Fatalf("UnsignedToSigned(max) = (%d, %v)", value, ok)
	}
	if value, ok := FloatToSigned(-2_147_483_648).GetOk(); !ok || value != math.MinInt32 {
		t.Fatalf("FloatToSigned(min) = (%d, %v)", value, ok)
	}
	if value, ok := FloatToSize(pointerHalf).GetOk(); !ok || value != 1<<(strconv.IntSize-1) {
		t.Fatalf("FloatToSize(high bit) = (%d, %v)", value, ok)
	}
	if value, ok := SignedToFloat(math.MinInt64).GetOk(); !ok || value != -0x1p63 {
		t.Fatalf("SignedToFloat(min) = (%v, %v)", value, ok)
	}
	if value, ok := NarrowFloat(math.MaxFloat32).GetOk(); !ok || value != math.MaxFloat32 {
		t.Fatalf("NarrowFloat(max) = (%v, %v)", value, ok)
	}
	if NarrowOrZero(3_000_000_000) != 0 || NarrowOrZero(42) != 42 {
		t.Fatal("NarrowOrZero did not take its else branch exactly on failure")
	}
}

func TestTranslatedEscapesKeepFoundationControlFlow(t *testing.T) {
	tests := []struct {
		name string
		got  int32
		want int32
	}{
		{"classify empty", Classify(NewShapeEmpty()), -1},
		{"classify large", Classify(NewShapeSquare(200)), 100},
		{"classify square", Classify(NewShapeSquare(3)), 10},
		{"classify named", Classify(NewShapeNamed("x")), 8},
		{"sum stop", SumUntilStop([]int32{1, 2, -2, 3, -1, 50}), 6},
		{"sum all", SumUntilStop([]int32{4, -3, 5}), 9},
		{"early return", EarlyConditional(true, 42), 42},
		{"early value", EarlyConditional(true, 4), 10},
		{"early else", EarlyConditional(false, 4), 0},
		{"assign continue", AssignEscape([]int32{3, 0, 9, 0}), 9},
		{"assign break", AssignEscape([]int32{3, 60, 9}), 3},
		{"tail none", TailEscape(NewOptionI32None()), 0},
		{"tail some", TailEscape(NewOptionI32Some(41)), 42},
		{"discard", DiscardEscape([]int32{1, 2, 0, 40}), 3},
		{"nested none", NestedEscape(NewOptionI32None(), 5), 1},
		{"nested limit", NestedEscape(NewOptionI32Some(9), 5), 6},
		{"nested return", NestedEscape(NewOptionI32Some(5000), 5), -1},
		{"nested inner", NestedEscape(NewOptionI32Some(3), 5), 4},
		{"guard high", GuardFallthrough(NewOptionI32Some(30)), 20},
		{"guard middle", GuardFallthrough(NewOptionI32Some(15)), 15},
		{"guard low", GuardFallthrough(NewOptionI32Some(1)), 101},
		{"guard none", GuardFallthrough(NewOptionI32None()), -5},
		{"let else ok", LetElseInArm(NewOptionI32Some(2), 40), 42},
		{"let else failure", LetElseInArm(NewOptionI32Some(2), 5_000_000_000), -1},
		{"let else none", LetElseInArm(NewOptionI32None(), 5_000_000_000), 0},
	}
	for _, test := range tests {
		if test.got != test.want {
			t.Errorf("%s = %d, want %d", test.name, test.got, test.want)
		}
	}
}

func TestTranslatedOwnersKeepValueSemantics(t *testing.T) {
	if got := MovePoint(3); got != 8 {
		t.Fatalf("MovePoint(3) = %d, want 8", got)
	}
	if got := EditOwner(); got != 11 {
		t.Fatalf("EditOwner() = %d, want 11", got)
	}
	if got := ReplaceChild(); got != 106 {
		t.Fatalf("ReplaceChild() = %d, want 106", got)
	}
	if got := DestructureOwner(); got != 20 {
		t.Fatalf("DestructureOwner() = %d, want 20", got)
	}
	point := MakePoint(4, 5)
	if got := ConsumePoint(point); got != 20 || point.X != 4 || point.Y != 5 {
		t.Fatalf("ConsumePoint(%#v) = %d", point, got)
	}
	tree := BuildTree(2)
	copied := tree
	if got := SumTree(GraftLeft(tree, 7)); got != 9 {
		t.Fatalf("SumTree(GraftLeft(tree, 7)) = %d, want 9", got)
	}
	if SumTree(tree) != 4 || SumTree(copied) != 4 {
		t.Fatalf("GraftLeft changed its source to %d and %d", SumTree(tree), SumTree(copied))
	}
	branch, ok := tree.GetNode()
	if !ok {
		t.Fatal("BuildTree(2) did not return a node")
	}
	branch.Left = NewTreeLeaf(100)
	if got := SumTree(tree); got != 4 {
		t.Fatalf("editing a payload copy changed the tree to %d", got)
	}
	if got := SumTree(NewTreeNode(branch)); got != 102 {
		t.Fatalf("SumTree(edited branch) = %d, want 102", got)
	}
	if _, ok := NewTreeLeaf(1).GetNode(); ok {
		t.Fatal("GetNode reported a leaf as a node")
	}
	if got := ListSum(BuildList(4)); got != 10 {
		t.Fatalf("ListSum(BuildList(4)) = %d, want 10", got)
	}
	list := NewOptionOwnListNodeSome(ListNode{Value: 5, Next: NewOptionOwnListNodeNone()})
	if ListSum(Prepend(list, 1)) != 6 || ListSum(list) != 5 {
		t.Fatal("Prepend changed the list it consumed")
	}
}

func TestTranslatedConsumingReceiversUseGoValues(t *testing.T) {
	if got := SpendTwice(20); got != 13 {
		t.Fatalf("SpendTwice(20) = %d, want 13", got)
	}
	if got := OwnedReceiver(); got != 10 {
		t.Fatalf("OwnedReceiver() = %d, want 10", got)
	}
	wallet := Wallet{Owner: "go", Coins: 20}
	spent := wallet.Spend(3)
	if spent.Owner != "go" || spent.Coins != 17 {
		t.Fatalf("wallet.Spend(3) = %#v", spent)
	}
	if got := wallet.Total(); got != 20 || wallet.Coins != 20 {
		t.Fatalf("the consumed original changed to %#v", wallet)
	}
}

var (
	_ Valued      = Coin{}
	_ Scored      = MetricWrapper{}
	_ Accumulator = &Counter{}
)

func TestTranslatedContractsKeepFoundationDispatch(t *testing.T) {
	tests := []struct {
		name string
		got  int32
		want int32
	}{
		{"named", NamedValues(), 42},
		{"doubled", Doubled(), 404210},
		{"described", Described(), 10011008},
		{"accumulate", Accumulate(), 42},
		{"owned valued", OwnedValued(), 4540},
		{"held valued", HeldValued(), 7},
		{"heterogeneous", Heterogeneous(), 3099},
		{"optional some", OptionalValued(true), 6},
		{"optional none", OptionalValued(false), 0},
	}
	for _, test := range tests {
		if test.got != test.want {
			t.Errorf("%s = %d, want %d", test.name, test.got, test.want)
		}
	}
	counter := Counter{value: 40}
	AddTwiceTo(&counter, 1)
	if counter.value != 42 {
		t.Fatalf("AddTwiceTo left the edited value at %d", counter.value)
	}
	coin := MakeCoin(4)
	copied := coin
	concrete, ok := coin.(Coin)
	if !ok {
		t.Fatalf("MakeCoin returned %T", coin)
	}
	concrete.stored = 9
	if ReadWorth(coin) != 4 || ReadWorth(copied) != 4 || ReadWorth(concrete) != 9 {
		t.Fatal("an owned contract shared its value with a Go copy")
	}
}

func TestTranslatedStaticAndDynamicDefaultsShareOneMethod(t *testing.T) {
	if got := StaticDoubled(); got != 2420 {
		t.Fatalf("StaticDoubled() = %d, want 2420", got)
	}
	if got := StaticDescribed(); got != 2016 {
		t.Fatalf("StaticDescribed() = %d, want 2016", got)
	}
	if got := StaticAccumulate(); got != 42 {
		t.Fatalf("StaticAccumulate() = %d, want 42", got)
	}
	var scored Scored = MetricWrapper{inner: Metric{stored: 6}}
	if got := scored.doubled(); got != 12 {
		t.Fatalf("MetricWrapper.doubled() = %d, want 12", got)
	}
	var labeled Labeled = OverridingTag{inner: Tag{stored: 7}}
	if got := labeled.described(); got != 1008 {
		t.Fatalf("OverridingTag.described() = %d, want 1008", got)
	}
}

// behavior.out holds the output of the same Report function compiled by the C backend.
func TestTranslatedReportMatchesFoundationBackends(t *testing.T) {
	want, err := os.ReadFile("behavior.out")
	if err != nil {
		t.Fatal(err)
	}
	reader, writer, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = writer
	Report()
	os.Stdout = stdout
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	got, err := io.ReadAll(reader)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("Report wrote\n%s\nwant\n%s", got, want)
	}
}

func TestTranslatedIteratorLoopsKeepFoundationOrder(t *testing.T) {
	if got := SumCountdown(4); got != 10 {
		t.Fatalf("SumCountdown(4) = %d, want 10", got)
	}
	if got := SumCountdown(0); got != 0 {
		t.Fatalf("SumCountdown(0) = %d, want 0", got)
	}
	if got := IndexedCountdown(6); got != 6543 {
		t.Fatalf("IndexedCountdown(6) = %d, want 6543", got)
	}
	if got := StopCountdown(); got != 2 {
		t.Fatalf("StopCountdown() = %d, want 2", got)
	}
	source := Countdown{Remaining: 3}
	if got := DrainCountdown(&source); got != 6 || source.Remaining != 0 {
		t.Fatalf("DrainCountdown() = %d with remaining %d", got, source.Remaining)
	}
}

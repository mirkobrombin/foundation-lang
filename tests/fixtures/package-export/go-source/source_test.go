package sample_source

import "testing"

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

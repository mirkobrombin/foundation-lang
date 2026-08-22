package main

import (
	"fmt"
	"reflect"
	"strings"

	"github.com/mirkobrombin/go-foundation/v2/core/serializer"
)

type person struct {
	FirstName string
	LastName  string
	Age       int
}

type maybeScore struct {
	Name  string
	Score *int
}

type tagged struct {
	Name   string `json:"name"`
	Secret string `json:"-"`
}

type customID string

type customIDConverter struct{}

func (customIDConverter) Encode(value any) (any, error) {
	id, ok := value.(customID)
	if !ok {
		return nil, fmt.Errorf("unexpected custom ID type")
	}
	return "id:" + string(id), nil
}

func (customIDConverter) Decode(value any) (any, error) {
	encoded, ok := value.(string)
	if !ok || !strings.HasPrefix(encoded, "id:") {
		return nil, fmt.Errorf("invalid custom ID")
	}
	return customID(strings.TrimPrefix(encoded, "id:")), nil
}

type customModel struct {
	ID customID
}

func encode(policy *serializer.Policy, value any) string {
	encoded, err := policy.MarshalToString(value)
	if err != nil {
		panic(err)
	}
	return encoded
}

func main() {
	value := person{FirstName: "Ada", LastName: "Lovelace", Age: 30}
	fmt.Println(encode(serializer.New(), value))
	fmt.Println(encode(serializer.New(serializer.WithNaming(serializer.SnakeCase)), value))
	fmt.Println(encode(serializer.New(serializer.WithNaming(serializer.CamelCase)), value))
	fmt.Println(encode(serializer.New(serializer.WithIgnoreNil()), maybeScore{Name: "Ada"}))
	fmt.Println(encode(serializer.New(serializer.WithIgnoreZero()),
		person{FirstName: "Ada"}))
	fmt.Println(encode(serializer.New(), tagged{Name: "Ada"}))
	fmt.Println(encode(serializer.New(serializer.WithCustomType(
		reflect.TypeOf(customID("")),
		func() serializer.Converter { return customIDConverter{} },
	)), customModel{ID: "42"}))

	var decoded person
	if err := serializer.New().Unmarshal([]byte(`{"Age":5,"Unknown":1}`), &decoded); err != nil {
		panic(err)
	}
	fmt.Println(decoded.Age)

	var invalid person
	err := serializer.New().Unmarshal([]byte(`{"Age":"wrong"}`), &invalid)
	fmt.Println(err != nil)
}

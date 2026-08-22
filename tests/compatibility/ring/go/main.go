package main

import (
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/ring"
)

func rejectsZero() (rejected bool) {
	defer func() {
		if recover() != nil {
			rejected = true
		}
	}()
	ring.New[int](0)
	return false
}

func main() {
	buffer := ring.New[int](3)
	fmt.Println(buffer.Cap())
	fmt.Println(buffer.Len())
	fmt.Println(buffer.Space())
	fmt.Println(buffer.Push(1))
	fmt.Println(buffer.Push(2))
	fmt.Println(buffer.Push(3))
	if buffer.Push(4) {
		fmt.Println(-1)
	} else {
		fmt.Println(4)
	}
	peeked, ok := buffer.Peek()
	if !ok {
		peeked = -1
	}
	fmt.Println(peeked)
	fmt.Println(buffer.Len())
	popped, ok := buffer.Pop()
	if !ok {
		popped = -1
	}
	fmt.Println(popped)
	fmt.Println(buffer.Push(4))
	for _, value := range buffer.Drain() {
		fmt.Println(value)
	}
	_, ok = buffer.Pop()
	if ok {
		fmt.Println(0)
	} else {
		fmt.Println(-1)
	}
	fmt.Println(rejectsZero())

	bytes := ring.NewBytes(4)
	fmt.Println(bytes.Write([]byte("hello")))
	fmt.Println(bytes.Len())
	first := make([]byte, 3)
	fmt.Println(bytes.Read(first))
	for _, value := range first {
		fmt.Println(value)
	}
	fmt.Println(bytes.Write([]byte("!!")))
	second := make([]byte, 4)
	read := bytes.Read(second)
	fmt.Println(read)
	for _, value := range second[:read] {
		fmt.Println(value)
	}
	bytes.Reset()
	fmt.Println(bytes.Len())
	fmt.Println(bytes.Space())
	fmt.Println("ring compatibility ok")
}

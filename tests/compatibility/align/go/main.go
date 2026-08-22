package main

import (
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/align"
)

func main() {
	fmt.Printf("u8:%d:%d\n", align.Up[uint8](5, 4), align.Down[uint8](5, 4))
	fmt.Printf("u16:%d:%d\n", align.Up[uint16](257, 256), align.Down[uint16](511, 256))
	fmt.Printf("u32:%d:%d\n", align.Up[uint32](65537, 65536), align.Down[uint32](131071, 65536))
	fmt.Printf("u64:%d:%d\n", align.Up[uint64](4294967297, 4294967296), align.Down[uint64](8589934591, 4294967296))
	fmt.Printf("usize:%d:%d\n", align.Up[uintptr](0x1234, 0x100), align.Down[uintptr](0x1234, 0x100))
}

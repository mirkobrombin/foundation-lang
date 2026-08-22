package main

import (
	"bytes"
	"encoding/base64"
	"fmt"
	"io"

	"github.com/mirkobrombin/go-foundation/v2/core/cpio"
)

func main() {
	var archive bytes.Buffer
	writer := cpio.NewWriter(&archive, cpio.WithUIDGID(7, 9), cpio.WithMTimeUnix(11))
	if err := writer.AddDir("etc", 0755); err != nil {
		panic(err)
	}
	if err := writer.AddFile("etc/foundation.conf", 0644, []byte("foundation\n")); err != nil {
		panic(err)
	}
	if err := writer.Close(); err != nil {
		panic(err)
	}
	fmt.Printf("archive:%s\n", base64.StdEncoding.EncodeToString(archive.Bytes()))

	reader := cpio.NewReader(bytes.NewReader(archive.Bytes()))
	directory, err := reader.Next()
	if err != nil {
		panic(err)
	}
	fmt.Printf("entry:%s:%d:%d:%d:%d:%d:%d\n", directory.Name, directory.Mode,
		directory.UID, directory.GID, directory.NLink, directory.MTime, directory.FileSize)
	file, err := reader.Next()
	if err != nil {
		panic(err)
	}
	fmt.Printf("entry:%s:%d:%d:%d:%d:%d:%d:%t\n", file.Name, file.Mode,
		file.UID, file.GID, file.NLink, file.MTime, file.FileSize,
		string(file.Data) == "foundation\n")
	_, err = reader.Next()
	fmt.Printf("end:%t\n", err == io.EOF)

	limitedWriter := cpio.NewWriter(io.Discard, cpio.WithWriterLimits(3, 10, 10))
	fmt.Printf("writer-file-limit:%t\n",
		limitedWriter.AddFile("large", 0644, []byte("1234")) != nil)
	fileLimited := cpio.NewReader(bytes.NewReader(archive.Bytes()),
		cpio.WithReaderLimits(4096, 10, 512<<20))
	_, err = fileLimited.Next()
	if err != nil {
		panic(err)
	}
	_, err = fileLimited.Next()
	fmt.Printf("reader-file-limit:%t\n", err != nil)
	entryLimited := cpio.NewReader(bytes.NewReader(archive.Bytes()),
		cpio.WithReaderMaxEntries(1))
	_, err = entryLimited.Next()
	if err != nil {
		panic(err)
	}
	_, err = entryLimited.Next()
	fmt.Printf("reader-entry-limit:%t\n", err != nil)
}

package main

import (
	"fmt"
	"strings"

	"github.com/mirkobrombin/go-foundation/v2/core/telemetry"
)

func main() {
	const encoded = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"
	context, err := telemetry.ParseTraceparent(encoded)
	if err != nil {
		panic(err)
	}
	fmt.Println("traceparent=" + context.Encode())

	_, upperErr := telemetry.ParseTraceparent(
		"00-0AF7651916CD43DD8448EB211C80319C-b7ad6b7169203331-01",
	)
	_, zeroErr := telemetry.ParseTraceparent(
		"00-00000000000000000000000000000000-b7ad6b7169203331-01",
	)
	fmt.Printf("invalid=%t\n", upperErr != nil && zeroErr != nil)

	counter := telemetry.NewPrometheusExporter()
	counter.IncCounter("requests_total", 5)
	printExporter(counter)

	gauge := telemetry.NewPrometheusExporter()
	gauge.SetGauge("active_workers", 3.5)
	printExporter(gauge)

	histogram := telemetry.NewPrometheusExporter()
	for _, value := range []float64{0.5, 7, 20} {
		histogram.ObserveHistogram("duration", value, []float64{5, 10, 1})
	}
	printExporter(histogram)
}

func printExporter(exporter *telemetry.PrometheusExporter) {
	var output strings.Builder
	exporter.WriteText(&output)
	fmt.Println(output.String())
}

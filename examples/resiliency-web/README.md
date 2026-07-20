# Stateful web rate limiting

The manual router owns one token bucket per client. Its smoke test exhausts the first client's
two-request burst, checks the following HTTP 429 response, then proves that another client still
has an independent allowance.

```sh
../../build/dev/foundationc run .
```

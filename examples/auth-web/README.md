# Authenticated web handler

This example signs a HMAC token before calling a Bearer-protected web
handler. Authentication gives the handler a typed `auth.Payload`, without a context key or a cast
at the point of use.

```sh
../../build/dev/foundationc run .
```

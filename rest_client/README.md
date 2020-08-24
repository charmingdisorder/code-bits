# rest_client: HTTP POST request in C

## Summary

This is piece of software that demonstrates using low-level socket functionality in C to send HTTP requests. POST requests of certain kind are used

    {"id":0, "jsonrpc":"2.0","method":"systemInfo","params":[]}

The number of methods is fixed and defined statically inline.

## Usage

The `rest_client` run syntax is

    ./rest_client [-h] [-i ID] [-j JSONRPC] URL METHOD [PARAMS]...

    -i ID, specifies ID for JSON request (0 used by default)
    -j JSONRPC, specifies JSONRPC value for request ("2.0" used by default)
    -h, print usage information

## Example

    $ ./rest_client -i 4 http://postman-echo.com/post systeminfo \"test\" 1.0
    json_string:
    {"id":4, "jsonrpc":"2.0","method":"systeminfo","params":["test",1.0]}
    HTTP/1.1 200 OK
    Date: Sun, 23 Aug 2020 19:19:43 GMT
    Content-Type: application/json; charset=utf-8
    Content-Length: 529
    Connection: close
    ETag: W/"211-VvQUfMCq5EjAXsMFCFKTaTEapdA"
    Vary: Accept-Encoding
    set-cookie: sails.sid=s%3AFA-eUFIgZ2UWtXyMl0vLHYAM1CK20pAI.f5tISwf%2F6lBH
    qGuRYLf%2BaaCB%2FYe9AtNzCxm4E4tNVrg; Path=/; HttpOnly

    {"args":{},"data":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":
    ["test",1]},"files":{},"form":{},"headers":{"x-forwarded-proto":"http","x
    -forwarded-port":"80","host":"a0207c42-pmecho-pmechoingr-f077-962204340.u
    s-east-1.elb.amazonaws.com","x-amzn-trace-id":"Root=1-5f42c14f-677908691c
    5f89d97081ee45","content-length":"69","content-type":"application/json"},
    "json":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"url":"http://a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.amazonaws.com/post"
    }TTP/1.1 200 OK
    Date: Sun, 23 Aug 2020 19:19:43 GMT
    Content-Type: application/json; charset=utf-8
    Content-Length: 529
    Connection: close
    ETag: W/"211-VvQUfMCq5EjAXsMFCFKTaTEapdA"
    Vary: Accept-Encoding
    set-cookie: sails.sid=s%3AFA-eUFIgZ2UWtXyMl0vLHYAM1CK20pAI.f5tISwf%2F6lBHqGuRYLf%2BaaCB%2FYe9AtNzCxm4E4tNVrg; Path=/; HttpOnly

    {"args":{},"data":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"files":{},"form":{},"headers":{"x-forwarded-proto":"http","x-forwarded-port":"80","host":"a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.amazonaws.com","x-amzn-trace-id":"Root=1-5f42c14f-677908691c5f89d97081ee45","content-length":"69","content-type":"application/json"},"json":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"url":"http://a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.amazonaws.com/post"
